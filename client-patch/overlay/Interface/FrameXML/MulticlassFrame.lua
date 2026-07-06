MulticlassUI = MulticlassUI or {};
local PREFIX = "MCLS";

-- Key Bindings UI strings for the (unbound-by-default) binding declared in Bindings.xml.
BINDING_HEADER_MULTICLASS = "Multiclass";
BINDING_NAME_TOGGLEMULTICLASS = "Toggle class panel";

UIPanelWindows["MulticlassFrame"] = { area = "left", pushable = 0, whileDead = 1 };

local function Send(payload)
	SendAddonMessage(PREFIX, payload, "WHISPER", UnitName("player"));
end

function MulticlassUI:Send(payload)
	Send(payload);
end

function ToggleMulticlassFrame()
	if ( MulticlassFrame:IsShown() ) then
		HideUIPanel(MulticlassFrame);
	else
		ShowUIPanel(MulticlassFrame);
	end
end

-- Parse "state <v> <en> <cap> <proj> id:level:slot ..." into a table (slot -1 = benched).
local function ParseState(message)
	local t = { classes = {} };
	local it = string.gmatch(message, "%S+");
	it();                              -- "state"
	t.v      = tonumber(it()) or 1;
	t.enable = tonumber(it()) == 1;
	t.cap    = tonumber(it()) or 1;
	t.proj   = tonumber(it()) or 0;
	for tuple in it do
		local id, lvl, slot = string.match(tuple, "(%d+):(%d+):(-?%d+)");
		if ( id ) then
			table.insert(t.classes, { id = tonumber(id), level = tonumber(lvl), slot = tonumber(slot) });
		end
	end
	return t;
end

function MulticlassUI:OnFrameLoad(frame)
	frame:RegisterEvent("CHAT_MSG_ADDON");
	frame:RegisterEvent("PLAYER_ENTERING_WORLD");
end

function MulticlassUI:OnFrameEvent(event, arg1, arg2)
	if ( event == "PLAYER_ENTERING_WORLD" ) then
		Send("hello");                 -- request the authoritative snapshot on login
	elseif ( event == "CHAT_MSG_ADDON" ) then
		local prefix, message = arg1, arg2;
		if ( prefix ~= PREFIX ) then
			return;
		end
		local verb = string.match(message, "^(%S+)");
		if ( verb == "state" ) then
			self.state = ParseState(message);
			if ( MulticlassFrame:IsShown() ) then
				self:Render();
			end
			if ( type(UpdateMicroButtons) == "function" ) then
				UpdateMicroButtons();
			end
		elseif ( verb == "err" ) then
			local text = string.match(message, "^err%s+(.*)");
			UIErrorsFrame:AddMessage(text or "Class change failed.", 1.0, 0.1, 0.1, 1.0);
		end
	end
end

function MulticlassUI:OnShow()
	Send("hello");                     -- refresh on open
	self:Render();
end

local CLASS_TOKEN = { [1] = "WARRIOR", [2] = "PALADIN", [3] = "HUNTER", [4] = "ROGUE", [5] = "PRIEST",
                       [6] = "DEATHKNIGHT", [7] = "SHAMAN", [8] = "MAGE", [9] = "WARLOCK", [11] = "DRUID" };
local ALL_CLASSES = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 11 };

-- Canonical, context-independent class abbreviations. Hardcoded so a class always renders the same
-- regardless of what else is active (War is always Warrior, Wlk always Warlock); computed truncation is
-- only a fallback for a class with no entry here (e.g. one a mod adds). Both tiers are collision-free; the
-- short tier is 1 char except where a collision forces 2 (Wl/Pr/DK).
local CLASS_ABBR3 = { [1] = "War", [2] = "Pld", [3] = "Hnt", [4] = "Rog", [5] = "Prs",
                       [6] = "DK", [7] = "Shm", [8] = "Mag", [9] = "Wlk", [11] = "Drd" };
local CLASS_ABBR2 = { [1] = "W", [2] = "P", [3] = "H", [4] = "R", [5] = "Pr",
                       [6] = "DK", [7] = "S", [8] = "M", [9] = "Wl", [11] = "D" };

-- Square class emblems from the character-create sheet (4x4 grid), keyed by classId.
local CLASS_ICON_FILE = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-Classes";
local CLASS_ICON_TC = {
	[1]  = { 0,    0.25, 0,    0.25 }, [2]  = { 0,    0.25, 0.5,  0.75 },
	[3]  = { 0,    0.25, 0.25, 0.5  }, [4]  = { 0.5,  0.75, 0,    0.25 },
	[5]  = { 0.5,  0.75, 0.25, 0.5  }, [6]  = { 0.25, 0.5,  0.5,  0.75 },
	[7]  = { 0.25, 0.5,  0.25, 0.5  }, [8]  = { 0.25, 0.5,  0,    0.25 },
	[9]  = { 0.75, 1.0,  0.25, 0.5  }, [11] = { 0.75, 1.0,  0,    0.25 },
};

local function ClassName(id)
	return LOCALIZED_CLASS_NAMES_MALE[CLASS_TOKEN[id]] or ("Class " .. id);
end

local function ClassColor(id)
	return RAID_CLASS_COLORS[CLASS_TOKEN[id]] or { r = 1, g = 1, b = 1 };
end

-- Composite identity label. full @1 | 3-char @2-3 | short @4-5 | Adventurer @6+.
local function Abbrev(id, width)
	local hard = (width == 3) and CLASS_ABBR3 or CLASS_ABBR2;
	if ( hard[id] ) then
		return hard[id];
	end
	local letters = ClassName(id):gsub("%s+", ""):sub(1, width);   -- fallback: truncate the name
	return letters:sub(1, 1):upper() .. letters:sub(2):lower();
end

local function IdentityLabel(order)
	local n = #order;
	if ( n == 0 ) then return "" end
	if ( n == 1 ) then return ClassName(order[1]) end
	if ( n >= 6 ) then return "Adventurer" end
	local width = (n <= 3) and 3 or 2;
	local parts = {};
	for _, id in ipairs(order) do
		table.insert(parts, Abbrev(id, width));
	end
	return table.concat(parts, "/");
end

-- Derive (owned map, active classIds in slot order) from the last state snapshot.
local function BuildView(st)
	local owned, active = {}, {};
	for _, c in ipairs(st.classes) do
		owned[c.id] = c;
		if ( c.slot and c.slot >= 0 ) then
			table.insert(active, c);
		end
	end
	table.sort(active, function(a, b) return a.slot < b.slot end);
	local order = {};
	for _, c in ipairs(active) do
		table.insert(order, c.id);
	end
	return owned, order;
end

local function IndexOf(t, v)
	for i, x in ipairs(t) do
		if ( x == v ) then return i end
	end
	return nil;
end

-- Every panel action reduces to sending the whole desired active set; the server validates and answers
-- with a fresh state that re-renders us. No optimistic local mutation.
local function SendOrder(order)
	if ( #order > 0 ) then
		MulticlassUI:Send("setorder " .. table.concat(order, " "));
	end
end

local function Toast(text)
	UIErrorsFrame:AddMessage(text, 1.0, 0.1, 0.1, 1.0);
end

local function ToggleClass(id)
	local st = MulticlassUI.state;
	if ( not st ) then return end
	local _, order = BuildView(st);
	local at = IndexOf(order, id);
	if ( at ) then
		if ( #order <= 1 ) then
			return Toast("You must keep at least one active class.");
		end
		table.remove(order, at);       -- deactivate
	else
		if ( #order >= st.cap ) then
			return Toast("That's more classes than you can keep active.");
		end
		table.insert(order, id);       -- activate (joins the end)
	end
	SendOrder(order);
end

-- 5-column grid of narrow tiles; the class name wraps ("Death Knight (80)" -> "Death" / "Knight (80)").
local COLS, TILE_W, TILE_H = 5, 70, 72;
local START_X, START_Y, DX, DY = 20, -66, 76, 80;

local tiles = {};

local function EnsureTile(i)
	if ( tiles[i] ) then return tiles[i] end
	local b = CreateFrame("Button", "MulticlassTile" .. i, MulticlassFrame);
	b:SetSize(TILE_W, TILE_H);

	-- rounded border (its curved corners match the rounded ButtonHilight-Square glow); tinted per state
	b:SetBackdrop({ edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", edgeSize = 14 });

	b.grad = b:CreateTexture(nil, "BACKGROUND");   -- vertical gradient fill (depth), tucked inside the border
	b.grad:SetPoint("TOPLEFT", 4, -4);
	b.grad:SetPoint("BOTTOMRIGHT", -4, 4);
	b.grad:SetTexture("Interface\\Buttons\\WHITE8x8");

	b.icon = b:CreateTexture(nil, "ARTWORK");
	b.icon:SetSize(32, 32);
	b.icon:SetPoint("TOP", 0, -7);
	b.icon:SetTexture(CLASS_ICON_FILE);

	-- explicit width => wraps to a 2nd line (anchors alone do not, in 3.3.5)
	b.label = b:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall");
	b.label:SetWidth(TILE_W - 12);
	b.label:SetPoint("BOTTOM", 0, 6);
	b.label:SetJustifyH("CENTER");

	b:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD");
	tiles[i] = b;
	return b;
end

function MulticlassUI:Render()
	local st = self.state;
	if ( not st ) then return end
	local owned, order = BuildView(st);

	local activeSet = {};
	for _, id in ipairs(order) do
		activeSet[id] = true;
	end

	-- display order: active (slot order), then benched-owned, then locked - all in the fixed grid.
	local seq = {};
	for _, id in ipairs(order) do
		table.insert(seq, id);
	end
	for _, id in ipairs(ALL_CLASSES) do
		if ( owned[id] and not activeSet[id] ) then table.insert(seq, id) end
	end
	for _, id in ipairs(ALL_CLASSES) do
		if ( not owned[id] ) then table.insert(seq, id) end
	end

	for idx, id in ipairs(seq) do
		local b = EnsureTile(idx);
		local col = (idx - 1) % COLS;
		local row = math.floor((idx - 1) / COLS);
		b:ClearAllPoints();
		b:SetPoint("TOPLEFT", MulticlassFrame, "TOPLEFT", START_X + col * DX, START_Y - row * DY);
		b.icon:SetTexCoord(unpack(CLASS_ICON_TC[id]));

		local c = ClassColor(id);
		local isActive = activeSet[id];
		local isOwned = owned[id] ~= nil;
		local levelText = isOwned and (" (" .. owned[id].level .. ")") or "";
		b.label:SetText(ClassName(id) .. levelText);

		if ( isActive ) then
			b.grad:SetGradientAlpha("VERTICAL", 0.03, 0.03, 0.05, 0.95, c.r * 0.22, c.g * 0.22, c.b * 0.22, 0.95);
			b:SetBackdropBorderColor(c.r, c.g, c.b, 1);
			b.icon:SetDesaturated(false);
			b.icon:SetAlpha(1);
			b.label:SetTextColor(c.r, c.g, c.b);
			b:EnableMouse(true);
			b:SetScript("OnClick", function() ToggleClass(id) end);
		elseif ( isOwned ) then
			b.grad:SetGradientAlpha("VERTICAL", 0.05, 0.05, 0.06, 0.9, 0.13, 0.12, 0.11, 0.9);
			b:SetBackdropBorderColor(0.42, 0.39, 0.35, 1);
			b.icon:SetDesaturated(false);
			b.icon:SetAlpha(0.5);
			b.label:SetTextColor(c.r * 0.6, c.g * 0.6, c.b * 0.6);
			b:EnableMouse(true);
			b:SetScript("OnClick", function() ToggleClass(id) end);
		else
			b.grad:SetGradientAlpha("VERTICAL", 0.03, 0.03, 0.04, 0.9, 0.08, 0.08, 0.10, 0.9);
			b:SetBackdropBorderColor(0.28, 0.28, 0.32, 1);
			b.icon:SetDesaturated(true);
			b.icon:SetAlpha(0.3);
			b.label:SetTextColor(0.45, 0.45, 0.45);
			b:EnableMouse(false);
			b:SetScript("OnClick", nil);
		end
		b:Show();
	end
	for i = #seq + 1, #tiles do
		tiles[i]:Hide();
	end

	MulticlassFrameIdentity:SetText("|cffffd100" .. IdentityLabel(order) .. "|r");
	MulticlassFrameHint:SetText("|cffc0c0c0" .. #order .. " of " .. st.cap .. " active|r"
		.. "\n|cff808080Click a class to activate or deactivate|r");
end
