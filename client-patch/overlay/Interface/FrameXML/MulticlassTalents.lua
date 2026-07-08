-- Multiclass Talents tab: a custom multi-tree renderer driven by MulticlassTalentData + the "talents" wire.
MulticlassUI = MulticlassUI or {};
MulticlassUI.talentState = MulticlassUI.talentState or {};   -- [classId] = { free, ranks = {[talentId]=count} }

-- "talents <classId> <free> <talentId:count> ..." -> store; re-render if this class's tab is showing.
function MulticlassUI:OnTalentsMessage(message)
	local it = string.gmatch(message, "%S+");
	it();                                       -- "talents"
	local classId = tonumber(it());
	local free = tonumber(it());
	if ( not classId ) then return end
	local ranks = {};
	for pair in it do
		local tid, rank = string.match(pair, "(%d+):(%d+)");
		if ( tid ) then ranks[tonumber(tid)] = tonumber(rank) end
	end
	self.talentState[classId] = { free = free or 0, ranks = ranks };
	if ( MulticlassFrame:IsShown() and self.activeTab == "talents" ) then
		self:RenderTalents();
	end
end

local BTN = 32;                          -- native TALENT_BUTTON_SIZE (positioning grid)
local PITCH = 63;                        -- native grid pitch
local OFF_X, OFF_Y = 23, 20;             -- OFF_X shifted ~1/3 icon left of native 35 to clear the in-gutter scrollbar
local PER_TIER = 5;                      -- native PLAYER_TALENTS_PER_TIER
local SCROLL_W, SCROLL_H = 288, 452;     -- per-tree viewport (native single-tree is 296x332; fit 3 side by side)
local TREE_W = 296;                      -- per-tree slot spacing
local TREE_X0 = 16;                      -- first tree's left inset (clears the panel border)
local TREE_TOP = -56;                    -- trees start below the header + per-tree labels
local TRACK_TEX = "Interface\\PaperDollInfoFrame\\UI-Character-ScrollBar";
local sel = {};                          -- right-edge class-selector buttons (pool)
local trees = {};                        -- per-tree-slot state

-- native tree background: 4 quads sized to the viewport, each showing an edge-fade slice of its texture.
local QUAD_TC = {
	TopLeft     = { 0, 1.0,    0, 1.0 },
	TopRight    = { 0, 0.6875, 0, 1.0 },
	BottomLeft  = { 0, 1.0,    0, 0.5859375 },
	BottomRight = { 0, 0.6875, 0, 0.5859375 },
};

local function EnsureTree(slot)
	if ( trees[slot] ) then return trees[slot] end
	local scroll = CreateFrame("ScrollFrame", "MulticlassTreeScroll" .. slot, MulticlassTalentsTab, "UIPanelScrollFrameTemplate");
	scroll:SetSize(SCROLL_W, SCROLL_H);
	scroll:EnableMouseWheel(true);
	scroll:SetScript("OnMouseWheel", function(self, delta)
		local new = self:GetVerticalScroll() - delta * 40;
		local max = self:GetVerticalScrollRange();
		if ( new < 0 ) then new = 0 elseif ( new > max ) then new = max end
		self:SetVerticalScroll(new);
	end);

	-- Static tree background on the scroll frame (native anchoring: right 40px + bottom 76px are edge slices).
	local bg = {};
	bg.TopLeft = scroll:CreateTexture(nil, "BORDER");
	bg.TopLeft:SetPoint("TOPLEFT", scroll, "TOPLEFT", 0, 0);
	bg.TopLeft:SetPoint("RIGHT", scroll, "RIGHT", -40, 0);
	bg.TopLeft:SetPoint("BOTTOM", scroll, "BOTTOM", 0, 76);
	bg.TopRight = scroll:CreateTexture(nil, "BORDER");
	bg.TopRight:SetPoint("TOPRIGHT", scroll, "TOPRIGHT", 0, 0);
	bg.TopRight:SetPoint("BOTTOMLEFT", bg.TopLeft, "BOTTOMRIGHT", 0, 0);
	bg.BottomLeft = scroll:CreateTexture(nil, "BORDER");
	bg.BottomLeft:SetPoint("BOTTOMLEFT", scroll, "BOTTOMLEFT", 0, 0);
	bg.BottomLeft:SetPoint("TOPRIGHT", bg.TopLeft, "BOTTOMRIGHT", 0, 0);
	bg.BottomRight = scroll:CreateTexture(nil, "BORDER");
	bg.BottomRight:SetPoint("BOTTOMRIGHT", scroll, "BOTTOMRIGHT", 0, 0);
	bg.BottomRight:SetPoint("TOPLEFT", bg.TopLeft, "BOTTOMRIGHT", 0, 0);

	-- Pull the scrollbar INSIDE the viewport's right gutter so it sits on the art, and give it a real groove.
	local sb = _G["MulticlassTreeScroll" .. slot .. "ScrollBar"];
	if ( sb ) then
		sb:ClearAllPoints();
		sb:SetPoint("TOPRIGHT", scroll, "TOPRIGHT", -4, -18);
		sb:SetPoint("BOTTOMRIGHT", scroll, "BOTTOMRIGHT", -4, 18);
		local track = scroll:CreateTexture(nil, "ARTWORK");
		track:SetTexture(TRACK_TEX);
		track:SetTexCoord(0, 0.484375, 0.42, 0.58);   -- middle groove slice (no end caps -> no notch)
		track:SetWidth(20);
		track:SetPoint("TOP", sb, "TOP", 0, 16);
		track:SetPoint("BOTTOM", sb, "BOTTOM", 0, -16);
	end

	local content = CreateFrame("Frame", "MulticlassTreeContent" .. slot, scroll);
	content:SetSize(SCROLL_W, SCROLL_H);
	scroll:SetScrollChild(content);

	-- Gold border around the whole tree (viewport + its scrollbar), tying them together.
	local border = CreateFrame("Frame", nil, MulticlassTalentsTab);
	border:SetPoint("TOPLEFT", scroll, "TOPLEFT", -3, 3);
	border:SetPoint("BOTTOMRIGHT", scroll, "BOTTOMRIGHT", 3, -3);
	border:SetBackdrop({ edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", edgeSize = 16 });
	border:SetBackdropBorderColor(0.9, 0.78, 0.5, 1);

	local t = { scroll = scroll, content = content, bg = bg, border = border, buttons = {}, lines = {} };
	t.label = MulticlassTalentsTab:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall");
	t.label:SetPoint("BOTTOMLEFT", border, "TOPLEFT", 3, 2);
	trees[slot] = t;
	return t;
end

local function SetTreeBackground(t, background)
	local base = "Interface\\TalentFrame\\" .. background .. "-";
	for suf, tc in pairs(QUAD_TC) do
		t.bg[suf]:SetTexture(base .. suf);
		t.bg[suf]:SetTexCoord(tc[1], tc[2], tc[3], tc[4]);
	end
end

-- Real native talent button: TalentButtonTemplate gives the slot ring + icon + rank border, exactly like the
-- default UI. State is shown via the Slot tint (green/gold/gray) + icon desaturation, matching TalentFrameBase.
local function EnsureButton(t, talentId)
	if ( t.buttons[talentId] ) then return t.buttons[talentId] end
	local name = "MulticlassTalentBtn" .. talentId;
	local b = CreateFrame("Button", name, t.content, "TalentButtonTemplate");
	b:SetScript("OnEnter", function(self)
		GameTooltip:SetOwner(self, "ANCHOR_RIGHT");
		if ( self.spellId ) then GameTooltip:SetHyperlink("spell:" .. self.spellId) end
		GameTooltip:Show();
	end);
	b.slotTex = _G[name .. "Slot"];
	b.rankText = _G[name .. "Rank"];
	b.rankBorder = _G[name .. "RankBorder"];
	t.buttons[talentId] = b;
	return b;
end

local function TalentXY(info)
	return info.col * PITCH + OFF_X + BTN / 2, -(info.tier * PITCH) - OFF_Y - BTN / 2;
end

local function DrawPrereq(t, info, depInfo, idx)
	local line = t.lines[idx];
	if ( not line ) then
		line = t.content:CreateTexture(nil, "ARTWORK");
		line:SetTexture("Interface\\Buttons\\WHITE8x8");
		line:SetVertexColor(1.0, 0.82, 0.1, 0.7);
		t.lines[idx] = line;
	end
	local x1, y1 = TalentXY(depInfo);   -- from prerequisite
	local x2, y2 = TalentXY(info);      -- to this talent
	line:ClearAllPoints();
	if ( math.abs(x2 - x1) < 4 ) then           -- vertical connector
		line:SetWidth(3);
		line:SetPoint("TOP", t.content, "TOPLEFT", x1, y1);
		line:SetPoint("BOTTOM", t.content, "TOPLEFT", x2, y2);
	else                                        -- horizontal / diagonal: run along the dependent's row
		line:SetHeight(3);
		line:SetPoint("LEFT", t.content, "TOPLEFT", math.min(x1, x2), y2);
		line:SetPoint("RIGHT", t.content, "TOPLEFT", math.max(x1, x2), y2);
	end
	line:Show();
end

local function RenderTree(slot, tabId, classId, xBase)
	local D = MulticlassTalentData;
	local tab = D.tabs[tabId];
	local st = MulticlassUI.talentState[classId] or { free = 0, ranks = {} };
	local ranks = st.ranks;
	local t = EnsureTree(slot);
	t.scroll:ClearAllPoints();
	t.scroll:SetPoint("TOPLEFT", MulticlassTalentsTab, "TOPLEFT", xBase, TREE_TOP);
	SetTreeBackground(t, tab.background);

	-- points spent in THIS tab (tier gate) + tallest tier (scroll-child height).
	local tabSpent, maxTier = 0, 0;
	for tid, info in pairs(D.talents) do
		if ( info.tab == tabId ) then
			if ( info.tier > maxTier ) then maxTier = info.tier end
			local r = ranks[tid];
			if ( r and r > 0 ) then tabSpent = tabSpent + r end
		end
	end
	t.content:SetHeight((maxTier + 1) * PITCH + OFF_Y + BTN + 8);
	t.label:SetText((tab.name or "Talents") .. " Talents: " .. tabSpent);

	for _, b in pairs(t.buttons) do b:Hide() end
	for tid, info in pairs(D.talents) do
		if ( info.tab == tabId ) then
			local b = EnsureButton(t, tid);
			local cur = ranks[tid] or 0;
			local maxR = #info.ranks;
			local spellId = info.ranks[math.min(cur + 1, maxR)];
			b.spellId = spellId;
			SetItemButtonTexture(b, (select(3, GetSpellInfo(spellId))) or "Interface\\Icons\\INV_Misc_QuestionMark");
			b.rankText:SetText(cur .. "/" .. maxR);   -- our variant: always show current/max (native shows only current)
			b:ClearAllPoints();
			b:SetPoint("TOPLEFT", t.content, "TOPLEFT", info.col * PITCH + OFF_X, -(info.tier * PITCH) - OFF_Y);

			-- native state model (TalentFrameBase): reachable -> green(<max)/gold(max); else -> gray + desaturated.
			local tierOk = (info.tier * PER_TIER) <= tabSpent;
			local prereqOk = (info.dep == 0) or ((ranks[info.dep] or 0) >= (info.depRank + 1));
			local reachable = tierOk and prereqOk and not ((st.free <= 0) and (cur == 0));
			b.rankBorder:Show();
			b.rankText:Show();
			if ( reachable ) then
				SetItemButtonDesaturated(b, nil);
				b.rankBorder:SetVertexColor(1, 1, 1);
				if ( cur < maxR ) then
					b.slotTex:SetVertexColor(0.1, 1.0, 0.1);
					b.rankText:SetTextColor(GREEN_FONT_COLOR.r, GREEN_FONT_COLOR.g, GREEN_FONT_COLOR.b);
				else
					b.slotTex:SetVertexColor(1.0, 0.82, 0);
					b.rankText:SetTextColor(NORMAL_FONT_COLOR.r, NORMAL_FONT_COLOR.g, NORMAL_FONT_COLOR.b);
				end
			else
				SetItemButtonDesaturated(b, 1, 0.65, 0.65, 0.65);
				b.slotTex:SetVertexColor(0.5, 0.5, 0.5);
				b.rankBorder:SetVertexColor(0.5, 0.5, 0.5);
				b.rankText:SetTextColor(GRAY_FONT_COLOR.r, GRAY_FONT_COLOR.g, GRAY_FONT_COLOR.b);
			end

			b:RegisterForClicks("LeftButtonUp", "RightButtonUp");
			b:SetScript("OnClick", function(self, button)
				if ( button == "RightButton" ) then
					-- free per-point removal; server cascades prereq/tier and re-pushes the whole class.
					if ( cur > 0 ) then MulticlassUI:Send("removetalent " .. classId .. " " .. tid) end
				else
					-- send the 0-based index being added == current count; server validates + re-pushes.
					MulticlassUI:Send("spendtalent " .. classId .. " " .. tid .. " " .. cur);
				end
			end);
			b:Show();
		end
	end

	-- prerequisite connectors.
	for _, l in ipairs(t.lines) do l:Hide() end
	local li = 0;
	for tid, info in pairs(D.talents) do
		if ( info.tab == tabId and info.dep and info.dep ~= 0 ) then
			local depInfo = D.talents[info.dep];
			if ( depInfo ) then li = li + 1; DrawPrereq(t, info, depInfo, li) end
		end
	end

	t.scroll:UpdateScrollChildRect();
	t.scroll:Show();
end

local function EnsureSelector(i)
	if ( sel[i] ) then return sel[i] end
	local b = CreateFrame("Button", "MulticlassTalentSel" .. i, MulticlassTalentsTab);
	b:SetSize(40, 40);
	b.bg = b:CreateTexture(nil, "BACKGROUND");
	b.bg:SetTexture("Interface\\Buttons\\UI-Quickslot2");
	b.bg:SetPoint("CENTER");
	b.bg:SetSize(62, 62);
	b.icon = b:CreateTexture(nil, "ARTWORK");
	b.icon:SetPoint("CENTER");
	b.icon:SetSize(30, 30);
	b.icon:SetTexture(MulticlassUI.CLASS_ICON_FILE);
	b.glow = b:CreateTexture(nil, "OVERLAY");
	b.glow:SetTexture("Interface\\Buttons\\UI-ActionButton-Border");
	b.glow:SetBlendMode("ADD");
	b.glow:SetPoint("CENTER");
	b.glow:SetSize(62, 62);
	b.glow:SetVertexColor(1.0, 0.82, 0.0);
	b:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD");
	sel[i] = b;
	return b;
end

local function EnsureHeader()
	if ( MulticlassTalentsHeader ) then return end
	local fs = MulticlassTalentsTab:CreateFontString("MulticlassTalentsHeader", "OVERLAY", "GameFontNormalLarge");
	fs:SetPoint("TOPLEFT", 16, -14);
	local rb = CreateFrame("Button", "MulticlassTalentsReset", MulticlassTalentsTab, "UIPanelButtonTemplate");
	rb:SetSize(90, 22);
	rb:SetPoint("TOPRIGHT", -13, -10);
	rb:SetText("Reset");
	rb:SetScript("OnClick", function()
		local c = MulticlassUI.talentSelectedClass;
		if ( c ) then StaticPopup_Show("MULTICLASS_RESET_TALENTS", MulticlassUI.ClassName(c), nil, c) end
	end);
end

StaticPopupDialogs["MULTICLASS_RESET_TALENTS"] = {
	text = "Reset all %s talents?",
	button1 = YES, button2 = NO,
	OnAccept = function(self, classId) MulticlassUI:Send("resettalents " .. classId) end,
	timeout = 0, whileDead = 1, hideOnEscape = 1,
};

function MulticlassUI:RenderTalents()
	local D = MulticlassTalentData;
	if ( not D or not self.state ) then return end
	EnsureHeader();
	local _, order = self.BuildView(self.state);   -- active classIds, slot order
	if ( #order == 0 ) then return end

	-- default / clamp the selected class to an active one.
	local selectedOk = false;
	for _, id in ipairs(order) do if ( id == self.talentSelectedClass ) then selectedOk = true end end
	if ( not selectedOk ) then self.talentSelectedClass = order[1] end
	local classId = self.talentSelectedClass;

	-- right-edge class selector (below the Reset button).
	for i, id in ipairs(order) do
		local b = EnsureSelector(i);
		b:ClearAllPoints();
		b:SetPoint("TOPRIGHT", MulticlassTalentsTab, "TOPRIGHT", -12, -48 - (i - 1) * 48);
		b.icon:SetTexCoord(unpack(self.CLASS_ICON_TC[id]));
		b.icon:SetDesaturated(id ~= classId);
		if ( id == classId ) then b.glow:Show() else b.glow:Hide() end
		b:SetScript("OnClick", function() MulticlassUI.talentSelectedClass = id; MulticlassUI:RenderTalents() end);
		b:Show();
	end
	for i = #order + 1, #sel do sel[i]:Hide() end

	-- composite header: selected class + its free points.
	local st = self.talentState[classId] or { free = 0 };
	local c = self.ClassColor(classId);
	MulticlassTalentsHeader:SetText(string.format("|cff%02x%02x%02x%s|r  -  %d points available",
		math.floor(c.r * 255), math.floor(c.g * 255), math.floor(c.b * 255), self.ClassName(classId), st.free or 0));

	-- the class's trees (page order), side by side, each in its own scroll panel.
	local tabIds = D.classTabs[classId] or {};
	for slot = 1, #trees do if ( trees[slot] ) then trees[slot].scroll:Hide() end end
	for slot, tabId in ipairs(tabIds) do
		RenderTree(slot, tabId, classId, TREE_X0 + (slot - 1) * TREE_W);
	end
end

-- Co-opt the talent keybind AND the stock Talents micro-button: both call ToggleTalentFrame(). Route them
-- to our Talents tab and never load/open the native PlayerTalentFrame. Gated on the feature being enabled;
-- when off, fall back to stock behavior so the client is byte-vanilla.
local _stockToggleTalentFrame = ToggleTalentFrame;
function ToggleTalentFrame()
	if ( not (MulticlassUI.state and MulticlassUI.state.enable) ) then
		if ( _stockToggleTalentFrame ) then return _stockToggleTalentFrame() end
		return;
	end
	if ( MulticlassFrame:IsShown() and MulticlassUI.activeTab == "talents" ) then
		HideUIPanel(MulticlassFrame);
	else
		ShowUIPanel(MulticlassFrame);
		MulticlassUI:SelectTab("talents");
	end
end
