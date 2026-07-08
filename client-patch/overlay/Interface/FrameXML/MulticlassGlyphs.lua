-- Multiclass Glyphs tab: a native-fidelity 6-socket glyph ring per active class, driven by the "glyphs" wire.
MulticlassUI = MulticlassUI or {};
MulticlassUI.glyphState = MulticlassUI.glyphState or {};   -- [classId] = { [slot] = { spell, enabled } }

-- "glyphs <classId> <slot>:<spell>:<enabled> ..." -> store; re-render if this class's ring is showing.
function MulticlassUI:OnGlyphsMessage(message)
	local it = string.gmatch(message, "%S+");
	it();                                       -- "glyphs"
	local classId = tonumber(it());
	if ( not classId ) then return end
	local slots = {};
	for tuple in it do
		local slot, spell, en = string.match(tuple, "(%d+):(%d+):(%d+)");
		if ( slot ) then
			slots[tonumber(slot)] = { spell = tonumber(spell), enabled = tonumber(en) == 1 };
		end
	end
	self.glyphState[classId] = slots;
	if ( MulticlassFrame:IsShown() and self.activeTab == "glyphs" ) then
		self:RenderGlyphs();
	end
end

-- Custom parchment (mc_glyph_bg): vanilla art with the Blizzard chrome removed, 331x395 in the top-left of a 512
-- atlas. Socket PIECES still come from the stock UI-GlyphFrame atlas (RING_TEX) via texcoords -- only the
-- background is ours. Socket centers are a regular hexagon (CX,CY + RADIUS at each socket's clock angle) sized
-- by SOCK. Type = GlyphSlot.dbc TypeFlags: majors = slots 0/3/5 (ids 1/4/6) at 12/4/8 o'clock, minors = slots
-- 1/2/4 (ids 2/3/5) at 6/10/2 -- alternating major/minor clockwise from 12.
local RING_TEX    = "Interface\\Spellbook\\UI-GlyphFrame";
local LOCKED_TEX  = "Interface\\Spellbook\\UI-GlyphFrame-Locked";
local BG_TEX      = "Interface\\Multiclass\\mc_glyph_bg";
local BG_W, BG_H  = 331, 395;
local BG_TCR, BG_TCB = 331 / 512, 395 / 512;   -- 0.646484375, 0.771484375
local SOCK        = 0.9;                        -- socket size vs vanilla
local CX, CY      = 161, 199;                   -- rune-circle center (bg pixels)
local RADIUS      = 123;                        -- socket-center radius from CX,CY
local UNLOCK_LVL  = { 15, 15, 50, 30, 70, 80 };   -- by UI socket id
-- per UI socket id: server slot, glyph type, clock angle (0 = 12 o'clock, degrees clockwise).
local SOCKETS = {
	[1] = { slot = 0, kind = "major", ang =   0 },   -- 12 o'clock
	[2] = { slot = 1, kind = "minor", ang = 180 },   --  6 o'clock
	[3] = { slot = 2, kind = "minor", ang = 300 },   -- 10 o'clock
	[4] = { slot = 3, kind = "major", ang = 120 },   --  4 o'clock
	[5] = { slot = 4, kind = "minor", ang =  60 },   --  2 o'clock
	[6] = { slot = 5, kind = "major", ang = 240 },   --  8 o'clock
};
-- inner rune backing texcoords: [0] empty, [1..6] per filled socket id (native GLYPH_SLOTS).
local RUNE_TC = {
	[0] = { 0.78125,     0.91015625,  0.69921875, 0.828125 },
	[1] = { 0,           0.12890625,  0.87109375, 1 },
	[2] = { 0.130859375, 0.259765625, 0.87109375, 1 },
	[3] = { 0.392578125, 0.521484375, 0.87109375, 1 },
	[4] = { 0.5234375,   0.65234375,  0.87109375, 1 },
	[5] = { 0.26171875,  0.390625,    0.87109375, 1 },
	[6] = { 0.654296875, 0.783203125, 0.87109375, 1 },
};
-- per-type setting/ring/rune sizes + tint (vanilla GlyphFrameGlyph_SetGlyphType); scaled by SOCK when drawn.
local STYLE = {
	major = { setW = 108, setTC = { 0.740234375, 0.953125, 0.484375, 0.697265625 }, ringW = 82, ringTC = { 0.767578125, 0.92578125, 0.32421875, 0.482421875 }, ringY = -1, runeW = 70, tint = { 1, 0.25, 0 } },
	minor = { setW = 86, setTC = { 0.765625, 0.927734375, 0.15625, 0.31640625 }, ringW = 62, ringTC = { 0.787109375, 0.908203125, 0.033203125, 0.154296875 }, ringY = 1, runeW = 64, tint = { 0, 0.25, 1 } },
};

local ring;
local sockets = {};
local gsel = {};

local function EnsureRing()
	if ( ring ) then return ring end
	ring = CreateFrame("Frame", "MulticlassGlyphRing", MulticlassGlyphsTab);
	ring:SetSize(BG_W, BG_H);
	ring:SetPoint("TOPLEFT", MulticlassGlyphsTab, "TOPLEFT", 16, -24);
	ring.bg = ring:CreateTexture(nil, "BACKGROUND");
	ring.bg:SetTexture(BG_TEX);
	ring.bg:SetTexCoord(0, BG_TCR, 0, BG_TCB);
	ring.bg:SetAllPoints(ring);
	-- gold border tying the parchment to the frame, same treatment as the talent trees.
	ring.border = CreateFrame("Frame", nil, MulticlassGlyphsTab);
	ring.border:SetPoint("TOPLEFT", ring, "TOPLEFT", -3, 3);
	ring.border:SetPoint("BOTTOMRIGHT", ring, "BOTTOMRIGHT", 3, -3);
	ring.border:SetBackdrop({ edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", edgeSize = 16 });
	ring.border:SetBackdropBorderColor(0.9, 0.78, 0.5, 1);
	ring.title = ring:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge");
	ring.title:SetPoint("BOTTOM", ring.border, "TOP", 0, 2);
	return ring;
end

local function EnsureSocket(id)
	if ( sockets[id] ) then return sockets[id] end
	local meta = SOCKETS[id];
	local sty = STYLE[meta.kind];
	local b = CreateFrame("Button", "MulticlassGlyphSocket" .. id, EnsureRing());
	b:SetSize(90 * SOCK, 90 * SOCK);
	local rad = meta.ang * math.pi / 180;
	b:SetPoint("CENTER", ring, "TOPLEFT", CX + RADIUS * math.sin(rad), -(CY - RADIUS * math.cos(rad)));
	b.meta = meta;

	b.setting = b:CreateTexture(nil, "BACKGROUND");
	b.setting:SetSize(sty.setW * SOCK, sty.setW * SOCK);
	b.setting:SetPoint("CENTER", 0, 0);

	b.rune = b:CreateTexture(nil, "BORDER");
	b.rune:SetTexture(RING_TEX);
	b.rune:SetSize(sty.runeW * SOCK, sty.runeW * SOCK);
	b.rune:SetPoint("CENTER", 0, 0);

	b.icon = b:CreateTexture(nil, "ARTWORK");
	b.icon:SetSize(53 * SOCK, 53 * SOCK);
	b.icon:SetPoint("CENTER", 0, 0);

	b.bezel = b:CreateTexture(nil, "OVERLAY");
	b.bezel:SetTexture(RING_TEX);
	b.bezel:SetSize(sty.ringW * SOCK, sty.ringW * SOCK);
	b.bezel:SetTexCoord(unpack(sty.ringTC));
	b.bezel:SetPoint("CENTER", 0, sty.ringY * SOCK);

	b:SetScript("OnEnter", function(self)
		GameTooltip:SetOwner(self, "ANCHOR_RIGHT");
		if ( self.spell ) then
			GameTooltip:SetHyperlink("spell:" .. self.spell);
		elseif ( self.locked ) then
			GameTooltip:SetText("Unlocked at level " .. UNLOCK_LVL[id]);
		else
			GameTooltip:SetText("Empty glyph slot");
			GameTooltip:AddLine("Drag a glyph onto this socket.", 0.6, 0.6, 0.6);
		end
		GameTooltip:Show();
	end);
	b:SetScript("OnLeave", function() GameTooltip:Hide() end);

	sockets[id] = b;
	return b;
end

-- Right-edge class selector, identical build to the talent tab's selector for cross-tab consistency.
local function EnsureGSel(i)
	if ( gsel[i] ) then return gsel[i] end
	local b = CreateFrame("Button", "MulticlassGlyphSel" .. i, MulticlassGlyphsTab);
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
	gsel[i] = b;
	return b;
end

local function RenderSocket(id, classId)
	local b = EnsureSocket(id);
	local sty = STYLE[b.meta.kind];
	local st = (MulticlassUI.glyphState[classId] or {})[b.meta.slot] or { spell = 0, enabled = false };
	b.spell = (st.spell and st.spell > 0) and st.spell or nil;
	b.locked = not st.enabled;

	if ( b.locked ) then
		b.setting:SetTexture(LOCKED_TEX);
		b.setting:SetTexCoord(0.1, 0.9, 0.1, 0.9);
		b.rune:Hide(); b.icon:Hide(); b.bezel:Hide();
	else
		b.setting:SetTexture(RING_TEX);
		b.setting:SetTexCoord(unpack(sty.setTC));
		b.bezel:Show();
		b.rune:Show();
		if ( b.spell ) then
			b.rune:SetTexCoord(unpack(RUNE_TC[id]));
			local _, _, icon = GetSpellInfo(b.spell);
			b.icon:SetTexture(icon);
			b.icon:SetVertexColor(unpack(sty.tint));
			b.icon:Show();
		else
			b.rune:SetTexCoord(unpack(RUNE_TC[0]));
			b.icon:Hide();
		end
	end
	b:Show();
end

function MulticlassUI:RenderGlyphs()
	if ( not self.state ) then return end
	if ( MulticlassGlyphsStubTitle ) then MulticlassGlyphsStubTitle:Hide() end
	if ( MulticlassGlyphsStubText ) then MulticlassGlyphsStubText:Hide() end
	EnsureRing();
	-- center the parchment between the panel's left border (inset 11) and the selector column (tabW - 54),
	-- so the gold border keeps even padding on both sides regardless of panel width.
	local selLeft = MulticlassGlyphsTab:GetWidth() - 54;
	ring:ClearAllPoints();
	ring:SetPoint("TOPLEFT", MulticlassGlyphsTab, "TOPLEFT", math.floor((11 + selLeft - BG_W) / 2), -24);
	local _, order = self.BuildView(self.state);   -- active classIds, slot order
	if ( #order == 0 ) then return end

	-- default / clamp the selected class to an active one.
	local selectedOk = false;
	for _, id in ipairs(order) do if ( id == self.glyphSelectedClass ) then selectedOk = true end end
	if ( not selectedOk ) then self.glyphSelectedClass = order[1] end
	local classId = self.glyphSelectedClass;

	for i, id in ipairs(order) do
		local b = EnsureGSel(i);
		b:ClearAllPoints();
		b:SetPoint("TOPRIGHT", MulticlassGlyphsTab, "TOPRIGHT", -14, -48 - (i - 1) * 48);
		b.icon:SetTexCoord(unpack(self.CLASS_ICON_TC[id]));
		b.icon:SetDesaturated(id ~= classId);
		if ( id == classId ) then b.glow:Show() else b.glow:Hide() end
		b:SetScript("OnClick", function() MulticlassUI.glyphSelectedClass = id; MulticlassUI:RenderGlyphs() end);
		b:Show();
	end
	for i = #order + 1, #gsel do gsel[i]:Hide() end

	local c = self.ClassColor(classId);
	ring.title:SetText(string.format("|cff%02x%02x%02x%s|r Glyphs",
		math.floor(c.r * 255), math.floor(c.g * 255), math.floor(c.b * 255), self.ClassName(classId)));

	for id = 1, 6 do RenderSocket(id, classId) end
end
