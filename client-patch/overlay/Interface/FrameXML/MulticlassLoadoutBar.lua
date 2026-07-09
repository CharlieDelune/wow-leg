-- Multiclass Loadouts quick-switch bar: a movable, per-character bar with one button per saved loadout.
-- Click a button to switch (out of combat); the active loadout is highlighted in place. Buttons hold a stable
-- saved order so their positions never move on a switch, and the bar grows with the loadout count (a small
-- collection is a small bar, never empty slots). Settings (show / columns / scale / position) live in an opaque
-- prefs blob persisted server-side and echoed back on login, so the bar follows the character.
MulticlassLoadoutBar = MulticlassLoadoutBar or {};
local BAR = MulticlassLoadoutBar;

local DEFAULT_ICON = "Interface\\Icons\\INV_Misc_QuestionMark";
local BTN, GAP, PAD = 40, 6, 8;              -- button size / inter-button gap / frame padding, at scale 1.0
local MIN_SCALE, MAX_SCALE = 0.6, 1.5;
local MAX_COLS = 12;

-- Mirrors the server blob "shown|cols|scale|point|relPoint|x|y". Defaults: hidden, single row (cols caps at
-- the loadout count anyway), scale 1.0, centred a little below the screen middle.
local prefs = { shown = false, cols = MAX_COLS, scale = 1.0, point = "CENTER", relPoint = "CENTER", x = 0, y = -160 };
local suppressSend = false;                  -- true while applying server/echoed state, so we do not loop it back
local frame, grip;
local buttons = {};

local function SerializePrefs()
	return string.format("%d|%d|%.2f|%s|%s|%d|%d",
		prefs.shown and 1 or 0, prefs.cols, prefs.scale, prefs.point, prefs.relPoint, prefs.x, prefs.y);
end

local function SendPrefs()
	if ( suppressSend ) then return end
	MulticlassUI:Send("setbarprefs " .. SerializePrefs());
end

-- Stable saved order (by sortOrder), NOT the panel's active-floated-to-top display order.
local function StableOrder()
	local list = {};
	for _, e in ipairs(MulticlassUI.loadoutList or {}) do table.insert(list, e) end
	table.sort(list, function(a, c) return (a.sortOrder or 0) < (c.sortOrder or 0) end);
	return list;
end

local function ButtonTooltip(self)
	local e = self.entry;
	if ( not e ) then return end
	GameTooltip:SetOwner(self, "ANCHOR_RIGHT");
	GameTooltip:SetText(e.name or "Loadout");
	local label = MulticlassIdentity and MulticlassIdentity.ColorLabel(e.classes or {}) or "";
	if ( label ~= "" ) then GameTooltip:AddLine(label) end
	if ( e.desc and e.desc ~= "" ) then GameTooltip:AddLine(e.desc, 0.8, 0.75, 0.6, true) end
	if ( e.active ) then
		GameTooltip:AddLine("Active loadout", 0.4, 0.8, 0.4);
	elseif ( InCombatLockdown() ) then
		GameTooltip:AddLine("Locked in combat", 0.8, 0.3, 0.3);
	else
		GameTooltip:AddLine("Click to switch", 0.4, 0.8, 0.4);
	end
	GameTooltip:Show();
end

local function StoreFramePosition()
	local p, _, rp, x, y = frame:GetPoint();
	prefs.point = p or "CENTER";
	prefs.relPoint = rp or prefs.point;
	prefs.x = math.floor((x or 0) + 0.5);
	prefs.y = math.floor((y or 0) + 0.5);
end

local function EnsureFrame()
	if ( frame ) then return frame end
	frame = CreateFrame("Frame", "MulticlassLoadoutBar", UIParent);
	frame:SetFrameStrata("MEDIUM");
	frame:SetClampedToScreen(true);
	frame:SetMovable(true);
	frame:EnableMouse(true);                       -- absorb clicks on the padding; the frame itself never drags
	frame:SetBackdrop({ bgFile = "Interface\\Buttons\\WHITE8x8",
		edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", edgeSize = 14,
		insets = { left = 4, right = 4, top = 4, bottom = 4 } });
	frame:SetBackdropColor(0.06, 0.05, 0.03, 0.82);
	frame:SetBackdropBorderColor(0.5, 0.42, 0.22, 1);

	-- Left grip is the ONLY drag target, so grabbing the bar to move it can never land on a button and switch.
	grip = CreateFrame("Frame", nil, frame);
	grip:EnableMouse(true);
	grip:RegisterForDrag("LeftButton");
	grip:SetScript("OnDragStart", function() if ( not InCombatLockdown() ) then frame:StartMoving() end end);
	grip:SetScript("OnDragStop", function() frame:StopMovingOrSizing(); StoreFramePosition(); SendPrefs() end);
	grip:SetScript("OnEnter", function(self)
		GameTooltip:SetOwner(self, "ANCHOR_TOPLEFT");
		GameTooltip:SetText("Loadouts bar");
		GameTooltip:AddLine("Drag to move.", 0.8, 0.8, 0.8);
		GameTooltip:Show();
	end);
	grip:SetScript("OnLeave", function() GameTooltip:Hide() end);
	grip.bg = grip:CreateTexture(nil, "BACKGROUND");
	grip.bg:SetAllPoints(grip);
	grip.bg:SetTexture(0.3, 0.26, 0.16, 0.35);     -- faint raised strip so the handle reads as grabbable
	grip.dots = {};
	for gi = 1, 6 do                               -- 2x3 grip dots, solid fills, sized/placed per scale in Render
		local dot = grip:CreateTexture(nil, "ARTWORK");
		dot:SetTexture(0.82, 0.74, 0.54, 0.95);
		grip.dots[gi] = dot;
	end

	frame:Hide();
	return frame;
end

local function EnsureButton(i)
	if ( buttons[i] ) then return buttons[i] end
	local b = CreateFrame("Button", "MulticlassLoadoutBarButton" .. i, frame);
	b:RegisterForClicks("LeftButtonUp");
	-- Same recipe as ActionButtonTemplate: icon under a slot bevel (UI-Quickslot2 NormalTexture, drawn larger
	-- than the icon) + a Depress pushed texture, so it reads AND feels like an action button, not a flat image.
	b.icon = b:CreateTexture(nil, "BACKGROUND");
	b.icon:SetAllPoints(b);
	b.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92);
	b:SetNormalTexture("Interface\\Buttons\\UI-Quickslot2");
	b.nt = b:GetNormalTexture();
	b:SetPushedTexture("Interface\\Buttons\\UI-Quickslot-Depress");
	b:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD");
	-- gold glow ring marking the active loadout; sized (in Render) to surround the icon like the equipped border.
	b.glow = b:CreateTexture(nil, "OVERLAY");
	b.glow:SetTexture("Interface\\Buttons\\UI-ActionButton-Border");
	b.glow:SetBlendMode("ADD");
	b.glow:SetPoint("CENTER");
	b.glow:SetVertexColor(1.0, 0.82, 0.0);
	b.glow:Hide();
	b:SetScript("OnEnter", ButtonTooltip);
	b:SetScript("OnLeave", function() GameTooltip:Hide() end);
	b:SetScript("OnClick", function(self)
		local e = self.entry;
		if ( not e or e.active ) then return end
		if ( InCombatLockdown() ) then
			UIErrorsFrame:AddMessage("Can't switch loadouts in combat.", 1.0, 0.1, 0.1, 1.0);
			return;
		end
		MulticlassUI:Send("switchloadout " .. e.id);
	end);
	buttons[i] = b;
	return b;
end

local function ApplyPosition()
	frame:ClearAllPoints();
	frame:SetPoint(prefs.point, UIParent, prefs.relPoint, prefs.x, prefs.y);
end

-- Rebuild from the current loadout list + prefs. Scale is applied to the GEOMETRY (button/gap/pad sizes), not
-- via frame:SetScale, so the anchor offsets stay in one coordinate space and the bar never drifts on rescale.
function BAR:Render()
	EnsureFrame();
	local list = StableOrder();
	local n = #list;
	if ( not prefs.shown or n == 0 ) then frame:Hide(); return end

	local combat = InCombatLockdown();
	local cols = math.max(1, math.min(prefs.cols, n));   -- never wider than the loadout count (no empty slots)
	local rows = math.ceil(n / cols);
	local s = prefs.scale;
	local btn, gap, pad = BTN * s, GAP * s, PAD * s;
	local gripW, gapG = 11 * s, 5 * s;                    -- drag-handle strip width + gap before the first button
	local x0 = pad + gripW + gapG;
	local baseLevel = frame:GetFrameLevel();

	for i = 1, n do
		local e = list[i];
		local b = EnsureButton(i);
		b.entry = e;
		b:SetSize(btn, btn);
		b:SetFrameLevel(baseLevel + (e.active and 4 or 2));   -- active glow draws above neighbouring slots
		b.icon:SetTexture(e.icon or DEFAULT_ICON);
		b.icon:SetDesaturated(combat and not e.active);
		b.nt:ClearAllPoints();
		b.nt:SetPoint("CENTER");
		b.nt:SetSize(btn * 66 / 36, btn * 66 / 36);       -- slot bevel, same proportion as ActionButtonTemplate
		b.glow:SetSize(btn * 62 / 36, btn * 62 / 36);      -- gold ring now surrounds the icon (was fixed, too small)
		if ( e.active ) then b.glow:Show() else b.glow:Hide() end
		local col = (i - 1) % cols;
		local row = math.floor((i - 1) / cols);
		b:ClearAllPoints();
		b:SetPoint("TOPLEFT", frame, "TOPLEFT", x0 + col * (btn + gap), -(pad + row * (btn + gap)));
		b:Show();
	end
	for i = n + 1, #buttons do buttons[i]:Hide() end

	frame:SetWidth(x0 + cols * btn + (cols - 1) * gap + pad);
	frame:SetHeight(pad * 2 + rows * btn + (rows - 1) * gap);

	-- grip strip down the left, full inner height, with a 2x3 dot cluster centred in it.
	grip:ClearAllPoints();
	grip:SetPoint("TOPLEFT", frame, "TOPLEFT", pad, -pad);
	grip:SetPoint("BOTTOMLEFT", frame, "BOTTOMLEFT", pad, pad);
	grip:SetWidth(gripW);
	local dotSz, dx, dy = math.max(2, 3 * s), 3 * s, 6 * s;
	for gi = 1, 6 do
		local d = grip.dots[gi];
		d:SetSize(dotSz, dotSz);
		d:ClearAllPoints();
		d:SetPoint("CENTER", grip, "CENTER", ((gi - 1) % 2 == 0) and -dx or dx, (1 - math.floor((gi - 1) / 2)) * dy);
	end

	ApplyPosition();
	frame:Show();
end

-- server -> client: apply the persisted blob (login/state push). Keep defaults if it is empty/malformed (a new
-- character has never saved one), and refresh the Loadouts-tab controls to match.
function BAR:ApplyServerPrefs(message)
	local blob = string.match(message, "^barprefs%s*(.*)$") or "";
	local shown, cols, scale, point, relPoint, x, y =
		string.match(blob, "^(%d+)|(%d+)|([%d.]+)|(%a+)|(%a+)|(-?%d+)|(-?%d+)$");
	suppressSend = true;
	if ( shown ) then
		prefs.shown = (shown == "1");
		prefs.cols = math.max(1, math.min(MAX_COLS, tonumber(cols) or MAX_COLS));
		prefs.scale = math.max(MIN_SCALE, math.min(MAX_SCALE, tonumber(scale) or 1.0));
		prefs.point, prefs.relPoint = point, relPoint;
		prefs.x, prefs.y = tonumber(x) or 0, tonumber(y) or 0;
	end
	self:Render();
	if ( MulticlassUI.RefreshBarSettings ) then MulticlassUI:RefreshBarSettings() end
	suppressSend = false;
end

-- Panel-driven setters (Loadouts-tab controls) + the toggle keybind. Each re-renders and persists.
function BAR:GetPrefs() return prefs end

function BAR:SetShown(shown)
	prefs.shown = shown and true or false;
	self:Render();
	SendPrefs();
end

function BAR:Toggle() self:SetShown(not prefs.shown) end

function BAR:SetColumns(c)
	prefs.cols = math.max(1, math.min(MAX_COLS, math.floor(c or MAX_COLS)));
	self:Render();
	SendPrefs();
end

function BAR:SetScale(sc)
	prefs.scale = math.max(MIN_SCALE, math.min(MAX_SCALE, sc or 1.0));
	self:Render();
	SendPrefs();
end

BAR.MIN_SCALE, BAR.MAX_SCALE, BAR.MAX_COLS = MIN_SCALE, MAX_SCALE, MAX_COLS;
