-- Multiclass Loadouts tab: quick-swap saved builds (slots + talents + glyphs + name/desc/icon).
-- Driven by the loadoutcap/loadout/loadoutclasses/loadoutdesc wire. Click a row's icon to switch. Rows show the
-- loadout's class emblems, carry Duplicate / Edit / Delete, and drag-and-drop reorder. Active pins to the top on
-- a gold ground; the rest hold their saved order. The tab also hosts the quick-switch-bar settings (see
-- MulticlassLoadoutBar.lua for the on-screen bar itself).
MulticlassUI = MulticlassUI or {};
MulticlassUI.loadoutList = MulticlassUI.loadoutList or {};   -- arrival order; each { id, sortOrder, active, icon, name, desc, classes }
MulticlassUI.loadoutById = MulticlassUI.loadoutById or {};
MulticlassUI.loadoutCap = MulticlassUI.loadoutCap or nil;    -- { capacity, purchased, nextCost }
MulticlassUI.loadoutScroll = 0;
MulticlassUI.pendingEdit = nil;                              -- { desc, icon } applied to the id echoed by loadoutnew

local DEFAULT_ICON = "Interface\\Icons\\INV_Misc_QuestionMark";

-- GetMacroIconInfo returns bare names on 3.3.5; SetTexture needs a full path. Normalise so a stored icon token
-- is always a full path (what we send, and what the row/preview draw directly).
local function NormalizeIcon(tex)
	if ( not tex or tex == "" or tex == "-" ) then return DEFAULT_ICON end
	if ( string.find(tex, "\\") ) then return tex end
	return "Interface\\Icons\\" .. tex;
end

local function Toast(text)
	UIErrorsFrame:AddMessage(text, 1.0, 0.1, 0.1, 1.0);
end

-- Single-line fit with an ellipsis: width-constrained FontStrings wrap in 3.3.5, so trim to width instead.
local function FitText(fs, text, maxW)
	fs:SetText(text);
	if ( fs:GetStringWidth() <= maxW ) then return end
	local lo, hi = 0, #text;
	while ( lo < hi ) do
		local mid = math.floor((lo + hi + 1) / 2);
		fs:SetText(string.sub(text, 1, mid) .. "...");
		if ( fs:GetStringWidth() <= maxW ) then lo = mid else hi = mid - 1 end
	end
	fs:SetText(string.sub(text, 1, lo) .. "...");
end

local function ClassColoredName(classId)
	local c = MulticlassUI.ClassColor(classId);
	return string.format("|cff%02x%02x%02x%s|r",
		math.floor(c.r * 255), math.floor(c.g * 255), math.floor(c.b * 255), MulticlassUI.ClassName(classId));
end

-- ---------------------------------------------------------------------------------------------------------
-- Delete / buy confirms. New and Duplicate open the full editor (below), not a bare name prompt.
-- ---------------------------------------------------------------------------------------------------------
StaticPopupDialogs["MULTICLASS_LOADOUT_DELETE"] = {
	text = "Delete loadout \"%s\"? This cannot be undone.",
	button1 = YES, button2 = NO,
	OnAccept = function(self) if ( self.data ) then MulticlassUI:Send("delloadout " .. self.data.id) end end,
	timeout = 0, whileDead = 1, hideOnEscape = 1,
};

StaticPopupDialogs["MULTICLASS_LOADOUT_BUY"] = {
	text = "Buy a permanent loadout slot for %s?",
	button1 = YES, button2 = NO,
	OnAccept = function() MulticlassUI:Send("buyloadoutslot") end,
	timeout = 0, whileDead = 1, hideOnEscape = 1,
};

-- ---------------------------------------------------------------------------------------------------------
-- Wire handlers. loadoutcap opens a fresh batch (reset the list); loadout/loadoutclasses/loadoutdesc fill it.
-- ---------------------------------------------------------------------------------------------------------
local function RenderIfShown()
	if ( MulticlassFrame:IsShown() and MulticlassUI.activeTab == "loadouts" ) then
		MulticlassUI:RenderLoadouts();
	end
end

function MulticlassUI:OnLoadoutCapMessage(message)
	local cap, purch, cost = string.match(message, "^loadoutcap (%d+) (%d+) (%d+)");
	if ( not cap ) then return end
	self.loadoutCap = { capacity = tonumber(cap), purchased = tonumber(purch), nextCost = tonumber(cost) };
	self.loadoutList = {};
	self.loadoutById = {};
	RenderIfShown();
end

local function LoadoutEntry(id)
	local e = MulticlassUI.loadoutById[id];
	if ( not e ) then
		e = { id = id, classes = {} };
		table.insert(MulticlassUI.loadoutList, e);
		MulticlassUI.loadoutById[id] = e;
	end
	return e;
end

function MulticlassUI:OnLoadoutMessage(message)
	local id, sortOrder, active, icon, name = string.match(message, "^loadout (%d+) (%d+) ([01]) (%S+)%s*(.*)$");
	if ( not id ) then return end
	local e = LoadoutEntry(tonumber(id));
	e.sortOrder = tonumber(sortOrder);
	e.active = (active == "1");
	e.icon = (icon ~= "-") and NormalizeIcon(icon) or nil;
	e.name = name or "";
	RenderIfShown();
end

function MulticlassUI:OnLoadoutClassesMessage(message)
	local id = tonumber(string.match(message, "^loadoutclasses (%d+)"));
	if ( not id ) then return end
	local e = LoadoutEntry(id);
	e.classes = {};
	for c in string.gmatch(string.match(message, "^loadoutclasses %d+%s*(.*)$") or "", "%d+") do
		table.insert(e.classes, tonumber(c));
	end
	RenderIfShown();
end

function MulticlassUI:OnLoadoutDescMessage(message)
	local id, desc = string.match(message, "^loadoutdesc (%d+)%s*(.*)$");
	if ( not id ) then return end
	local e = self.loadoutById[tonumber(id)];
	if ( e ) then e.desc = desc or ""; RenderIfShown() end
end

-- The server echoes the id of a just-created/duplicated loadout so the editor can apply its icon + description.
function MulticlassUI:OnLoadoutNewMessage(message)
	local id = tonumber(string.match(message, "^loadoutnew (%d+)"));
	if ( not id ) then return end
	local pe = self.pendingEdit;
	if ( pe ) then
		self:Send("iconloadout " .. id .. " " .. pe.icon);
		self:Send("descloadout " .. id .. " " .. pe.desc);
		self.pendingEdit = nil;
	end
end

-- ---------------------------------------------------------------------------------------------------------
-- Ordering. The saved order is flat and includes the active loadout at its real slot; the display only floats
-- the active one to the top. Drag-and-drop reorder (built in the render section, where the row frames live)
-- drops a loadout right after the visible row above it, so the active holds its slot and merely shifts down
-- when something lands ahead of it.
-- ---------------------------------------------------------------------------------------------------------
local function DisplayOrder()   -- active first (pinned), then the rest by saved sortOrder
	local list = {};
	for _, e in ipairs(MulticlassUI.loadoutList) do table.insert(list, e) end
	table.sort(list, function(a, c)
		if ( a.active ~= c.active ) then return a.active end
		return (a.sortOrder or 0) < (c.sortOrder or 0);
	end);
	return list;
end

local function SavedOrder()     -- flat saved order (active included at its slot), by sortOrder
	local list = {};
	for _, e in ipairs(MulticlassUI.loadoutList) do table.insert(list, e) end
	table.sort(list, function(a, c) return (a.sortOrder or 0) < (c.sortOrder or 0) end);
	return list;
end

-- ---------------------------------------------------------------------------------------------------------
-- Editor frame: name + description + macro-icon picker. Shared by New / Duplicate / Edit.
-- ---------------------------------------------------------------------------------------------------------
local ICON_COLS, ICON_ROWS = 8, 6;
local ICON_PER_PAGE = ICON_COLS * ICON_ROWS;
local ICON_SIZE, ICON_STEP = 30, 34;
local editFrame, iconButtons;
local editState = { mode = "edit", id = 0, srcId = 0, icon = DEFAULT_ICON, page = 0 };

local function RefreshIconGrid()
	local total = GetNumMacroIcons();
	local maxPage = math.floor(math.max(0, total - 1) / ICON_PER_PAGE);
	editState.page = math.max(0, math.min(editState.page, maxPage));
	local base = editState.page * ICON_PER_PAGE;
	for i = 1, ICON_PER_PAGE do
		local btn = iconButtons[i];
		local idx = base + i;
		if ( idx <= total ) then
			local tex = NormalizeIcon(GetMacroIconInfo(idx));
			btn.tex:SetTexture(tex);
			btn.iconPath = tex;
			if ( tex == editState.icon ) then btn.sel:Show() else btn.sel:Hide() end
			btn:Show();
		else
			btn:Hide();
		end
	end
	editFrame.pageText:SetText("Page " .. (editState.page + 1) .. " / " .. (maxPage + 1));
	editFrame.preview:SetTexture(editState.icon);
end

local function EnsureIconButton(i)
	if ( iconButtons[i] ) then return iconButtons[i] end
	local b = CreateFrame("Button", "MulticlassLoadoutIcon" .. i, editFrame);
	b:SetSize(ICON_SIZE, ICON_SIZE);
	local col = (i - 1) % ICON_COLS;
	local row = math.floor((i - 1) / ICON_COLS);
	b:SetPoint("TOPLEFT", editFrame, "TOPLEFT", 22 + col * ICON_STEP, -184 - row * ICON_STEP);
	b.tex = b:CreateTexture(nil, "ARTWORK");
	b.tex:SetAllPoints(b);
	b.sel = b:CreateTexture(nil, "OVERLAY");
	b.sel:SetTexture("Interface\\Buttons\\UI-ActionButton-Border");
	b.sel:SetBlendMode("ADD");
	b.sel:SetPoint("CENTER");
	b.sel:SetSize(ICON_SIZE + 20, ICON_SIZE + 20);
	b.sel:SetVertexColor(1.0, 0.82, 0.0);
	b.sel:Hide();
	b:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD");
	b:SetScript("OnClick", function(self) editState.icon = self.iconPath; RefreshIconGrid() end);
	iconButtons[i] = b;
	return b;
end

local function DoEditorSave()
	local name = string.gsub(editFrame.name:GetText() or "", "^%s*(.-)%s*$", "%1");
	if ( name == "" ) then return Toast("A loadout needs a name.") end
	local desc = editFrame.desc:GetText() or "";
	local icon = editState.icon;
	if ( editState.mode == "edit" ) then
		MulticlassUI:Send("renameloadout " .. editState.id .. " " .. name);
		MulticlassUI:Send("descloadout " .. editState.id .. " " .. desc);
		MulticlassUI:Send("iconloadout " .. editState.id .. " " .. icon);
	elseif ( editState.mode == "dup" ) then
		MulticlassUI.pendingEdit = { desc = desc, icon = icon };
		MulticlassUI:Send("duploadout " .. editState.srcId .. " " .. name);
	else   -- new
		MulticlassUI.pendingEdit = { desc = desc, icon = icon };
		MulticlassUI:Send("newloadout " .. name);
	end
	editFrame:Hide();
end

local function EnsureEditFrame()
	if ( editFrame ) then return editFrame end
	editFrame = CreateFrame("Frame", "MulticlassLoadoutEdit", UIParent);
	editFrame:SetFrameStrata("DIALOG");
	editFrame:SetSize(320, 468);
	editFrame:SetPoint("CENTER");
	editFrame:SetBackdrop({
		bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
		edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
		tile = true, tileSize = 32, edgeSize = 32,
		insets = { left = 11, right = 12, top = 12, bottom = 11 } });
	editFrame:EnableMouse(true);
	editFrame:SetMovable(true);
	editFrame:RegisterForDrag("LeftButton");
	editFrame:SetScript("OnDragStart", editFrame.StartMoving);
	editFrame:SetScript("OnDragStop", editFrame.StopMovingOrSizing);
	editFrame:Hide();
	tinsert(UISpecialFrames, "MulticlassLoadoutEdit");   -- ESC closes it

	editFrame.title = editFrame:CreateFontString(nil, "ARTWORK", "GameFontNormal");
	editFrame.title:SetPoint("TOP", 0, -16);
	editFrame.title:SetText("Edit Loadout");

	local nameLbl = editFrame:CreateFontString(nil, "ARTWORK", "GameFontNormalSmall");
	nameLbl:SetPoint("TOPLEFT", 22, -42);
	nameLbl:SetText("Name");
	editFrame.name = CreateFrame("EditBox", "MulticlassLoadoutEditName", editFrame, "InputBoxTemplate");
	editFrame.name:SetSize(268, 20);
	editFrame.name:SetPoint("TOPLEFT", nameLbl, "BOTTOMLEFT", 6, -4);
	editFrame.name:SetAutoFocus(false);
	editFrame.name:SetMaxLetters(32);

	local descLbl = editFrame:CreateFontString(nil, "ARTWORK", "GameFontNormalSmall");
	descLbl:SetPoint("TOPLEFT", nameLbl, "BOTTOMLEFT", 0, -44);
	descLbl:SetText("Description");
	editFrame.desc = CreateFrame("EditBox", "MulticlassLoadoutEditDesc", editFrame, "InputBoxTemplate");
	editFrame.desc:SetSize(268, 20);
	editFrame.desc:SetPoint("TOPLEFT", descLbl, "BOTTOMLEFT", 6, -4);
	editFrame.desc:SetAutoFocus(false);
	editFrame.desc:SetMaxLetters(128);

	local iconLbl = editFrame:CreateFontString(nil, "ARTWORK", "GameFontNormalSmall");
	iconLbl:SetPoint("TOPLEFT", descLbl, "BOTTOMLEFT", 0, -42);
	iconLbl:SetText("Icon");
	editFrame.preview = editFrame:CreateTexture(nil, "ARTWORK");
	editFrame.preview:SetSize(26, 26);
	editFrame.preview:SetPoint("LEFT", iconLbl, "RIGHT", 8, 0);

	-- Page nav shares the Icon-label row: one tight [ < | Page x/y | > ] cluster on the right. Chain
	-- LEFT->RIGHT off the label with 0 y so prev, the text, and next all sit on the label's centre line.
	editFrame.prev = CreateFrame("Button", nil, editFrame, "UIPanelButtonTemplate");
	editFrame.prev:SetSize(24, 22);
	editFrame.prev:SetPoint("LEFT", iconLbl, "LEFT", 150, 0);
	editFrame.prev:SetText("<");
	editFrame.prev:SetScript("OnClick", function() editState.page = editState.page - 1; RefreshIconGrid() end);
	editFrame.pageText = editFrame:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall");
	editFrame.pageText:SetSize(72, 22);
	editFrame.pageText:SetPoint("LEFT", editFrame.prev, "RIGHT", 4, 0);
	editFrame.pageText:SetJustifyH("CENTER");
	editFrame.pageText:SetJustifyV("MIDDLE");
	editFrame.next = CreateFrame("Button", nil, editFrame, "UIPanelButtonTemplate");
	editFrame.next:SetSize(24, 22);
	editFrame.next:SetPoint("LEFT", editFrame.pageText, "RIGHT", 4, 0);
	editFrame.next:SetText(">");
	editFrame.next:SetScript("OnClick", function() editState.page = editState.page + 1; RefreshIconGrid() end);

	iconButtons = {};
	for i = 1, ICON_PER_PAGE do EnsureIconButton(i) end

	editFrame.save = CreateFrame("Button", nil, editFrame, "UIPanelButtonTemplate");
	editFrame.save:SetSize(110, 24);
	editFrame.save:SetPoint("BOTTOMLEFT", 22, 16);
	editFrame.save:SetText("Save");
	editFrame.save:SetScript("OnClick", DoEditorSave);
	editFrame.cancel = CreateFrame("Button", nil, editFrame, "UIPanelButtonTemplate");
	editFrame.cancel:SetSize(110, 24);
	editFrame.cancel:SetPoint("BOTTOMRIGHT", -22, 16);
	editFrame.cancel:SetText("Cancel");
	editFrame.cancel:SetScript("OnClick", function() editFrame:Hide() end);

	return editFrame;
end

-- mode: "new" (blank), "dup" (fork `entry`), "edit" (`entry`).
local function OpenEditor(mode, entry)
	EnsureEditFrame();
	editState.mode = mode;
	if ( mode == "new" ) then
		editState.id, editState.srcId = 0, 0;
		editState.icon = DEFAULT_ICON;
		editFrame.title:SetText("New Loadout");
		editFrame.name:SetText("");
		editFrame.desc:SetText("");
	elseif ( mode == "dup" ) then
		editState.srcId = entry.id;
		editState.icon = entry.icon or DEFAULT_ICON;
		editFrame.title:SetText("Duplicate \"" .. (entry.name or "") .. "\"");
		editFrame.name:SetText((entry.name or "") .. " Copy");
		editFrame.desc:SetText(entry.desc or "");
	else
		editState.id = entry.id;
		editState.icon = entry.icon or DEFAULT_ICON;
		editFrame.title:SetText("Edit \"" .. (entry.name or "") .. "\"");
		editFrame.name:SetText(entry.name or "");
		editFrame.desc:SetText(entry.desc or "");
	end
	editState.page = 0;
	editFrame:Show();
	RefreshIconGrid();
end

-- ---------------------------------------------------------------------------------------------------------
-- Tab render. Fixed panel; the list fills the frame and scrolls by wheel when there are more rows than fit.
-- ---------------------------------------------------------------------------------------------------------
local PAD = 16;
local CAPBAR_H = 30;
local ROW_H = 52;
local ROW_GAP = 5;
local ROW_STEP = ROW_H + ROW_GAP;
local LIST_TOP = -(8 + CAPBAR_H + 10);

local capbar, listbg, newbtn;
local barcfg, barShow, barColsText, barScale, barScaleVal;   -- quick-switch-bar settings controls
local rows = {};
local dropLine, dragDriver;                                   -- assigned in EnsureChrome
local drag = { id = nil, moving = false, startY = 0, frame = nil };

-- Reorder is drag-and-drop, polled from a hidden OnUpdate frame so it never depends on the finicky
-- RegisterForDrag path. Grab a non-active row (grip or body), and on release it drops after the visible row
-- above the cursor. anchorId = the loadout to sit directly after (nil = becomes the first non-active).
local function ComputeDrop()
	local uiY = select(2, GetCursorPosition()) / UIParent:GetEffectiveScale();
	local anchorId, lineY, firstTop = nil, nil, nil;
	for _, b in ipairs(rows) do
		if ( b:IsShown() and b.entry and not b.entry.active and b.entry.id ~= drag.id ) then
			if ( not firstTop ) then firstTop = b:GetTop() end
			if ( uiY < (b:GetTop() + b:GetBottom()) / 2 ) then   -- this row sits above the cursor
				anchorId = b.entry.id;
				lineY = b:GetBottom();
			end
		end
	end
	return anchorId, (lineY or firstTop);
end

local function UpdateDropLine()
	local _, lineY = ComputeDrop();
	if ( not lineY or not listbg ) then dropLine:Hide(); return end
	local off = listbg:GetTop() - lineY;
	dropLine:ClearAllPoints();
	dropLine:SetPoint("TOPLEFT", listbg, "TOPLEFT", 6, -off);
	dropLine:SetPoint("TOPRIGHT", listbg, "TOPRIGHT", -6, -off);
	dropLine:Show();
end

local function FinishDrag()
	local anchorId = ComputeDrop();
	local moved = drag.id;
	local ids = {};
	for _, e in ipairs(SavedOrder()) do if ( e.id ~= moved ) then table.insert(ids, e.id) end end
	local pos = 1;
	if ( anchorId ) then
		for i, id in ipairs(ids) do if ( id == anchorId ) then pos = i + 1; break end end
	end
	table.insert(ids, pos, moved);       -- flat order, active included; server rewrites sortOrder from this
	MulticlassUI:Send("orderloadouts " .. table.concat(ids, " "));
end

local function BeginDrag(b)              -- from a row's OnMouseDown; only non-active rows reorder
	if ( not b.entry or b.entry.active ) then return end
	drag.id = b.entry.id;
	drag.moving = false;
	drag.startY = select(2, GetCursorPosition());
	drag.frame = b;
	dragDriver:Show();
end

local function EnsureChrome()
	if ( capbar ) then return end
	local tab = MulticlassLoadoutsTab;

	capbar = CreateFrame("Frame", "MulticlassLoadoutCapBar", tab);
	capbar:SetPoint("TOPLEFT", tab, "TOPLEFT", PAD, -8);
	capbar:SetHeight(CAPBAR_H);
	capbar:SetBackdrop({ bgFile = "Interface\\Buttons\\WHITE8x8",
		edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", edgeSize = 14,
		insets = { left = 3, right = 3, top = 3, bottom = 3 } });
	capbar:SetBackdropColor(0.08, 0.06, 0.03, 0.85);
	capbar:SetBackdropBorderColor(0.55, 0.45, 0.22, 1);
	capbar.slots = capbar:CreateFontString(nil, "OVERLAY", "GameFontHighlight");
	capbar.slots:SetPoint("LEFT", 10, 0);
	capbar.buy = CreateFrame("Button", "MulticlassLoadoutBuy", capbar, "UIPanelButtonTemplate");
	capbar.buy:SetSize(128, 22);
	capbar.buy:SetPoint("RIGHT", -6, 0);
	capbar.buy:SetScript("OnClick", function()
		local nc = MulticlassUI.loadoutCap and MulticlassUI.loadoutCap.nextCost or 0;
		StaticPopup_Show("MULTICLASS_LOADOUT_BUY", GetCoinTextureString(nc));
	end);

	listbg = CreateFrame("Frame", "MulticlassLoadoutList", tab);
	listbg:SetPoint("TOPLEFT", tab, "TOPLEFT", PAD, LIST_TOP);
	listbg:SetBackdrop({ bgFile = "Interface\\Buttons\\WHITE8x8",
		edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", edgeSize = 14,
		insets = { left = 3, right = 3, top = 3, bottom = 3 } });
	listbg:SetBackdropColor(0.10, 0.08, 0.05, 0.9);
	listbg:SetBackdropBorderColor(0.45, 0.37, 0.20, 1);

	dropLine = listbg:CreateTexture(nil, "OVERLAY");   -- gold "it lands here" indicator during a drag
	dropLine:SetTexture("Interface\\Buttons\\WHITE8x8");
	dropLine:SetVertexColor(0.96, 0.86, 0.5, 0.95);
	dropLine:SetHeight(3);
	dropLine:Hide();

	dragDriver = CreateFrame("Frame", nil, tab);       -- polls the mouse only while a row is being dragged
	dragDriver:Hide();
	dragDriver:SetScript("OnUpdate", function()
		if ( not drag.id ) then dragDriver:Hide(); return end
		if ( IsMouseButtonDown("LeftButton") ) then
			local cy = select(2, GetCursorPosition());
			if ( not drag.moving and math.abs(cy - drag.startY) > 8 ) then
				drag.moving = true;
				if ( drag.frame ) then drag.frame:SetAlpha(0.55) end   -- lift the dragged row
			end
			if ( drag.moving ) then UpdateDropLine() end
		else
			if ( drag.moving ) then FinishDrag() end
			if ( drag.frame ) then drag.frame:SetAlpha(1) end
			drag.id, drag.moving, drag.frame = nil, false, nil;
			dropLine:Hide();
			dragDriver:Hide();
		end
	end);

	newbtn = CreateFrame("Button", "MulticlassLoadoutNew", tab, "UIPanelButtonTemplate");
	newbtn:SetSize(160, 24);
	newbtn:SetText("+ New Loadout");
	newbtn:SetScript("OnClick", function() OpenEditor("new") end);

	tab:EnableMouseWheel(true);
	tab:SetScript("OnMouseWheel", function(_, delta)
		MulticlassUI.loadoutScroll = (MulticlassUI.loadoutScroll or 0) - delta;
		MulticlassUI:RenderLoadouts();
	end);

	tab.empty = tab:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall");
	tab.empty:SetPoint("TOP", listbg, "TOP", 0, -40);
	tab.empty:SetText("No loadouts yet. Click \"+ New Loadout\" to save this build.");
	tab.empty:SetWidth(300);
	tab.empty:Hide();

	-- Quick-switch bar settings, below the New button (the tab is sized taller so these never shrink the list).
	-- These drive MulticlassLoadoutBar and persist through the same wire; RefreshBarSettings syncs them to prefs.
	barcfg = CreateFrame("Frame", "MulticlassLoadoutBarCfg", tab);
	barcfg:SetPoint("TOPLEFT", listbg, "BOTTOMLEFT", 0, -44);
	barcfg:SetPoint("TOPRIGHT", listbg, "BOTTOMRIGHT", 0, -44);
	barcfg:SetHeight(116);
	barcfg:SetBackdrop({ bgFile = "Interface\\Buttons\\WHITE8x8",
		edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", edgeSize = 14,
		insets = { left = 3, right = 3, top = 3, bottom = 3 } });
	barcfg:SetBackdropColor(0.08, 0.06, 0.03, 0.85);
	barcfg:SetBackdropBorderColor(0.45, 0.37, 0.20, 1);

	local cfgTitle = barcfg:CreateFontString(nil, "OVERLAY", "GameFontNormal");
	cfgTitle:SetPoint("TOPLEFT", 10, -8);
	cfgTitle:SetText("|cffe9d6a4Quick-switch bar|r");

	barShow = CreateFrame("CheckButton", "MulticlassLoadoutBarShow", barcfg, "UICheckButtonTemplate");
	barShow:SetSize(24, 24);
	barShow:SetPoint("TOPLEFT", 8, -28);
	barShow:SetScript("OnClick", function(self)
		MulticlassLoadoutBar:SetShown(self:GetChecked() and true or false);
	end);
	local showLbl = barcfg:CreateFontString(nil, "OVERLAY", "GameFontHighlight");
	showLbl:SetPoint("LEFT", barShow, "RIGHT", 2, 0);
	showLbl:SetText("Show bar on screen");

	local colLbl = barcfg:CreateFontString(nil, "OVERLAY", "GameFontNormal");
	colLbl:SetPoint("TOPLEFT", 12, -62);
	colLbl:SetText("Columns");
	local colMinus = CreateFrame("Button", nil, barcfg, "UIPanelButtonTemplate");
	colMinus:SetSize(22, 22);
	colMinus:SetPoint("LEFT", colLbl, "LEFT", 70, 0);
	colMinus:SetText("-");
	colMinus:SetScript("OnClick", function()
		MulticlassLoadoutBar:SetColumns(MulticlassLoadoutBar:GetPrefs().cols - 1);
		MulticlassUI:RefreshBarSettings();
	end);
	barColsText = barcfg:CreateFontString(nil, "OVERLAY", "GameFontHighlight");
	barColsText:SetPoint("LEFT", colMinus, "RIGHT", 6, 0);
	barColsText:SetWidth(26);
	barColsText:SetJustifyH("CENTER");
	local colPlus = CreateFrame("Button", nil, barcfg, "UIPanelButtonTemplate");
	colPlus:SetSize(22, 22);
	colPlus:SetPoint("LEFT", barColsText, "RIGHT", 6, 0);
	colPlus:SetText("+");
	colPlus:SetScript("OnClick", function()
		MulticlassLoadoutBar:SetColumns(MulticlassLoadoutBar:GetPrefs().cols + 1);
		MulticlassUI:RefreshBarSettings();
	end);
	local colHint = barcfg:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall");
	colHint:SetPoint("LEFT", colPlus, "RIGHT", 8, 0);
	colHint:SetText("buttons per row");

	local scaleLbl = barcfg:CreateFontString(nil, "OVERLAY", "GameFontNormal");
	scaleLbl:SetPoint("TOPLEFT", 12, -94);
	scaleLbl:SetText("Scale");
	barScale = CreateFrame("Slider", "MulticlassLoadoutBarScale", barcfg, "OptionsSliderTemplate");
	barScale:SetWidth(150);
	barScale:SetPoint("LEFT", scaleLbl, "LEFT", 70, 0);
	barScale:SetMinMaxValues(MulticlassLoadoutBar.MIN_SCALE, MulticlassLoadoutBar.MAX_SCALE);
	barScale:SetValueStep(0.05);
	_G["MulticlassLoadoutBarScaleLow"]:SetText("");
	_G["MulticlassLoadoutBarScaleHigh"]:SetText("");
	_G["MulticlassLoadoutBarScaleText"]:SetText("");
	barScaleVal = barcfg:CreateFontString(nil, "OVERLAY", "GameFontHighlight");
	barScaleVal:SetPoint("LEFT", barScale, "RIGHT", 12, 0);
	barScale:SetScript("OnValueChanged", function(_, value)
		barScaleVal:SetText(math.floor(value * 100 + 0.5) .. "%");
		if ( MulticlassUI._barCfgRefreshing ) then return end
		MulticlassLoadoutBar:SetScale(value);
	end);
end

-- Sync the settings controls to the bar's current prefs (called on render and when the server pushes prefs).
-- Guarded so the slider's programmatic SetValue does not echo back through the wire.
function MulticlassUI:RefreshBarSettings()
	if ( not barShow ) then return end
	local p = MulticlassLoadoutBar:GetPrefs();
	self._barCfgRefreshing = true;
	barShow:SetChecked(p.shown);
	barColsText:SetText(p.cols);
	barScale:SetValue(p.scale);
	barScaleVal:SetText(math.floor(p.scale * 100 + 0.5) .. "%");
	self._barCfgRefreshing = false;
end

local function EnsureRowClassIcon(b, k)
	b.cls = b.cls or {};
	if ( b.cls[k] ) then return b.cls[k] end
	local c = CreateFrame("Button", nil, b);
	c:SetSize(17, 17);
	c.tex = c:CreateTexture(nil, "ARTWORK");
	c.tex:SetAllPoints(c);
	c.tex:SetTexture(MulticlassUI.CLASS_ICON_FILE);
	c:SetScript("OnEnter", function(self)
		GameTooltip:SetOwner(self, "ANCHOR_RIGHT");
		GameTooltip:SetText(ClassColoredName(self.classId), 1, 1, 1);
		GameTooltip:Show();
	end);
	c:SetScript("OnLeave", function() GameTooltip:Hide() end);
	b.cls[k] = c;
	return c;
end

local function EnsureRow(i)
	if ( rows[i] ) then return rows[i] end
	local b = CreateFrame("Frame", "MulticlassLoadoutRow" .. i, listbg);
	b:SetHeight(ROW_H);
	b:SetBackdrop({ edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", edgeSize = 12 });
	b.grad = b:CreateTexture(nil, "BACKGROUND");
	b.grad:SetPoint("TOPLEFT", 3, -3);
	b.grad:SetPoint("BOTTOMRIGHT", -3, 3);
	b.grad:SetTexture("Interface\\Buttons\\WHITE8x8");

	-- The whole row body is the drag handle for reorder; the grip is the affordance (hidden on the active row).
	b:EnableMouse(true);
	b:SetScript("OnMouseDown", function(_, button) if ( button == "LeftButton" ) then BeginDrag(b) end end);

	b.grip = CreateFrame("Frame", nil, b);
	b.grip:SetSize(14, 34);
	b.grip:SetPoint("LEFT", 6, 0);
	-- 2x3 grip dots. Solid-colour fills (not a downscaled WHITE8x8 bitmap) plus a shared TOPLEFT grid keep every
	-- square crisp and uniform: per-column x and per-row y offsets round the same way across all six dots.
	for gi = 1, 6 do
		local dot = b.grip:CreateTexture(nil, "ARTWORK");
		dot:SetTexture(0.74, 0.66, 0.48, 0.9);
		dot:SetSize(4, 4);
		dot:SetPoint("TOPLEFT", b.grip, "TOPLEFT", 2 + ((gi - 1) % 2) * 6, -(9 + math.floor((gi - 1) / 2) * 6));
	end

	-- switch icon (click to swap, drag to a bar for the one-press macro).
	b.icon = CreateFrame("Button", "MulticlassLoadoutRowIcon" .. i, b);
	b.icon:SetSize(40, 40);
	b.icon:SetPoint("LEFT", 24, 0);
	b.icon:EnableMouse(true);
	b.icon:RegisterForClicks("LeftButtonUp");
	b.icon.tex = b.icon:CreateTexture(nil, "ARTWORK");
	b.icon.tex:SetAllPoints(b.icon);
	b.icon.tex:SetTexCoord(0.07, 0.93, 0.07, 0.93);
	b.icon:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD");
	b.icon:SetScript("OnEnter", function(self)
		GameTooltip:SetOwner(self, "ANCHOR_RIGHT");
		GameTooltip:SetText(b.entry and b.entry.name or "Loadout");
		if ( b.entry and b.entry.desc and b.entry.desc ~= "" ) then
			GameTooltip:AddLine(b.entry.desc, 0.8, 0.75, 0.6, true);
		end
		if ( b.entry and b.entry.active ) then
			GameTooltip:AddLine("Active loadout.", 0.4, 0.8, 0.4);
		elseif ( InCombatLockdown() ) then
			GameTooltip:AddLine("Locked in combat.", 0.8, 0.3, 0.3);
		else
			GameTooltip:AddLine("Click to switch.", 0.4, 0.8, 0.4);
		end
		GameTooltip:Show();
	end);
	b.icon:SetScript("OnLeave", function() GameTooltip:Hide() end);
	b.icon:SetScript("OnClick", function()
		if ( not b.entry or b.entry.active ) then return end
		if ( InCombatLockdown() ) then return Toast("Can't switch loadouts in combat.") end
		MulticlassUI:Send("switchloadout " .. b.entry.id);
	end);

	-- name on the top line; the sub-line (emblems + description) is positioned in render since the emblem
	-- count varies. Both anchor to the row so they clear the icon column at x=72.
	b.name = b:CreateFontString(nil, "OVERLAY", "GameFontNormal");
	b.name:SetPoint("TOPLEFT", b, "TOPLEFT", 72, -8);
	b.name:SetJustifyH("LEFT");

	b.desc = b:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall");
	b.desc:SetJustifyH("LEFT");

	-- right-side action cluster, vertically centred, full size: Duplicate, Edit, Delete.
	local function ActionButton(tex, tip)
		local a = CreateFrame("Button", nil, b);
		a:SetSize(26, 26);
		a.tex = a:CreateTexture(nil, "ARTWORK");
		a.tex:SetPoint("TOPLEFT", 3, -3);
		a.tex:SetPoint("BOTTOMRIGHT", -3, 3);
		a.tex:SetTexture(tex);
		a:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD");
		a:SetScript("OnEnter", function(self) GameTooltip:SetOwner(self, "ANCHOR_RIGHT"); GameTooltip:SetText(tip); GameTooltip:Show() end);
		a:SetScript("OnLeave", function() GameTooltip:Hide() end);
		return a;
	end
	b.del = ActionButton("Interface\\RaidFrame\\ReadyCheck-NotReady", "Delete");
	b.del:SetPoint("RIGHT", -8, 0);
	b.edit = ActionButton("Interface\\Icons\\INV_Inscription_Tradeskill01", "Edit");
	b.edit:SetPoint("RIGHT", b.del, "LEFT", -5, 0);
	b.dup = ActionButton("Interface\\Icons\\INV_Misc_Book_09", "Duplicate");
	b.dup:SetPoint("RIGHT", b.edit, "LEFT", -5, 0);

	b.dup:SetScript("OnClick", function() if ( b.entry ) then OpenEditor("dup", b.entry) end end);
	b.edit:SetScript("OnClick", function() if ( b.entry ) then OpenEditor("edit", b.entry) end end);
	b.del:SetScript("OnClick", function()
		if ( b.entry ) then StaticPopup_Show("MULTICLASS_LOADOUT_DELETE", b.entry.name, nil, { id = b.entry.id }) end
	end);

	rows[i] = b;
	return b;
end

function MulticlassUI:RenderLoadouts()
	EnsureChrome();
	if ( MulticlassLoadoutsStubTitle ) then MulticlassLoadoutsStubTitle:Hide() end
	if ( MulticlassLoadoutsStubText ) then MulticlassLoadoutsStubText:Hide() end

	local tab = MulticlassLoadoutsTab;
	local innerW = tab:GetWidth() - 2 * PAD;
	local combat = InCombatLockdown();

	local cap = self.loadoutCap or { capacity = 0, purchased = 0, nextCost = 0 };
	local n = #self.loadoutList;
	capbar:SetWidth(innerW);
	capbar.slots:SetText("|cffe9d6a4" .. n .. "|r / " .. cap.capacity .. " slots used");
	capbar.buy:SetText("Buy Slot (" .. math.floor((cap.nextCost or 0) / 10000) .. "g)");

	-- fill the panel down to the New button, reserving the bottom for it + the quick-switch-bar settings block
	-- (the tab is 720 tall vs 600 so the list keeps its old height); scroll by wheel if more rows than fit.
	local listH = tab:GetHeight() + LIST_TOP - 164;
	listbg:SetWidth(innerW);
	listbg:SetHeight(listH);
	local maxVisible = math.max(1, math.floor((listH - 10) / ROW_STEP));

	local list = DisplayOrder();
	local offset = math.max(0, math.min(self.loadoutScroll or 0, math.max(0, n - maxVisible)));
	self.loadoutScroll = offset;

	local rowW = innerW - 12;
	local shown = 0;
	for vis = 1, math.min(maxVisible, n) do
		local d = vis + offset;         -- 1-based display index (active is always d == 1)
		local e = list[d];
		local b = EnsureRow(vis);
		b.entry = e;
		b:SetAlpha(1);                                   -- clear any leftover drag-dim
		b:SetWidth(rowW);
		b:ClearAllPoints();
		b:SetPoint("TOPLEFT", listbg, "TOPLEFT", 6, -6 - (vis - 1) * ROW_STEP);

		b.icon.tex:SetTexture(e.icon or DEFAULT_ICON);
		b.icon.tex:SetDesaturated(combat and not e.active);

		local classes = e.classes or {};
		local nclasses = #classes;
		local emblemsW = (nclasses > 0) and (nclasses * 20 + 6) or 0;
		local clusterLeft = rowW - 96;                   -- left edge of the 3 action buttons (26*3 + 2 gaps + margin)
		FitText(b.name, e.name or "", math.max(40, clusterLeft - 6 - 72));

		-- sub-line: class emblems on the left, description to their right, both clearing the action cluster.
		for k, classId in ipairs(classes) do
			local ci = EnsureRowClassIcon(b, k);
			ci.classId = classId;
			ci.tex:SetTexCoord(unpack(self.CLASS_ICON_TC[classId]));
			ci:ClearAllPoints();
			ci:SetPoint("TOPLEFT", b, "TOPLEFT", 72 + (k - 1) * 20, -30);
			ci:Show();
		end
		if ( b.cls ) then for k = nclasses + 1, #b.cls do b.cls[k]:Hide() end end

		b.desc:ClearAllPoints();
		b.desc:SetPoint("TOPLEFT", b, "TOPLEFT", 72 + emblemsW, -33);
		if ( e.desc and e.desc ~= "" ) then FitText(b.desc, e.desc, math.max(30, clusterLeft - 6 - (72 + emblemsW))) else b.desc:SetText("|cff70685a(no description)|r") end

		if ( e.active ) then
			b.grad:SetGradientAlpha("VERTICAL", 0.62, 0.55, 0.34, 0.95, 0.90, 0.82, 0.55, 0.98);
			b:SetBackdropBorderColor(0.95, 0.82, 0.4, 1);
			b.name:SetTextColor(0.22, 0.16, 0.05);
			b.desc:SetTextColor(0.42, 0.33, 0.15);
			b.icon:SetAlpha(1);
			b.grip:Hide();                              -- active pins to the top and never reorders
		else
			b.grad:SetGradientAlpha("VERTICAL", 0.05, 0.05, 0.06, 0.9, 0.14, 0.12, 0.09, 0.9);
			b:SetBackdropBorderColor(0.42, 0.36, 0.24, 1);
			b.name:SetTextColor(0.91, 0.84, 0.64);
			b.desc:SetTextColor(0.66, 0.6, 0.47);
			b.icon:SetAlpha(combat and 0.5 or 1);
			b.grip:Show();
		end

		-- Delete-active auto-switches first, so it locks in combat; inactive delete + dup + edit stay usable.
		if ( combat and e.active ) then b.del:Disable(); b.del.tex:SetVertexColor(0.5, 0.5, 0.5) else b.del:Enable(); b.del.tex:SetVertexColor(1, 1, 1) end

		b:Show();
		shown = vis;
	end
	for i = shown + 1, #rows do rows[i]:Hide() end

	if ( tab.empty ) then
		if ( n == 0 ) then tab.empty:Show() else tab.empty:Hide() end
	end

	newbtn:ClearAllPoints();
	newbtn:SetPoint("TOP", listbg, "BOTTOM", 0, -10);

	self:RefreshBarSettings();
end

-- ---------------------------------------------------------------------------------------------------------
-- /mcls slash: switch by id or name, or open the tab.
-- ---------------------------------------------------------------------------------------------------------
SLASH_MCLS1 = "/mcls";
SlashCmdList["MCLS"] = function(msg)
	local cmd, rest = string.match(msg or "", "^(%S*)%s*(.-)%s*$");
	cmd = string.lower(cmd or "");
	if ( cmd == "switch" and rest ~= "" ) then
		local id = tonumber(rest);
		if ( id ) then MulticlassUI:Send("switchloadout " .. id); return end
		for _, e in pairs(MulticlassUI.loadoutById or {}) do
			if ( e.name and string.lower(e.name) == string.lower(rest) ) then MulticlassUI:Send("switchloadout " .. e.id); return end
		end
		Toast("No loadout named \"" .. rest .. "\".");
	else
		ShowUIPanel(MulticlassFrame);
		MulticlassUI:SelectTab("loadouts");
	end
end
