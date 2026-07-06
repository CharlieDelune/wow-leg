-- Wires MulticlassIdentity's composite label into stock display surfaces via hooksecurefunc/
-- HookScript only, so tainted stock functions are never overwritten outright.
if ( not MulticlassIdentity ) then
	return;
end

-- Surface #1: character sheet (self). PaperDollFrame_SetLevel formats CharacterLevelText/
-- HonorLevelText from PLAYER_LEVEL ("Level %s %s %s" = level, race, class). hooksecurefunc runs
-- our override right after the original in the same frame, so the projected class never flashes.
local function ApplyPaperDollLevel()
	local label = MulticlassIdentity.GetColoredLabel(UnitName("player"));
	if ( not label ) then
		return;
	end
	local text = format(PLAYER_LEVEL, UnitLevel("player"), UnitRace("player"), label);
	CharacterLevelText:SetText(text);
	HonorLevelText:SetText(text);
end

hooksecurefunc("PaperDollFrame_SetLevel", ApplyPaperDollLevel);

-- Surface #2: unit mouseover/target tooltip. Unit tooltip content is built client-side, so the
-- only Lua seam is the post-build OnTooltipSetUnit script; find the line carrying the native
-- class word and splice in the composite label with a plain (non-pattern) substring replace. A
-- player tooltip's guild line ("<Guild Name>") is rendered before the class line, so a guild tag
-- that happens to contain a class word ("Mageborn", "Paladins of Light") would otherwise match
-- first and get corrupted while the real class line goes untouched; every TOOLTIP_UNIT_LEVEL_*
-- variant in GlobalStrings.lua that the client uses for a unit's secondary line starts with the
-- LEVEL token ("Level %s ..."), which a guild-name line never contains, so also requiring LEVEL
-- pins the match to the actual class line.
local function ApplyTooltipLabel(tooltip, unit)
	local nativeClass = UnitClass(unit);
	if ( not nativeClass ) then
		return;
	end
	local label = MulticlassIdentity.GetColoredLabel(UnitName(unit));
	if ( not label ) then
		return;
	end
	for i = 2, tooltip:NumLines() do   -- line 1 is always the unit name, never the class line
		local fs = _G["GameTooltipTextLeft"..i];
		local text = fs and fs:GetText();
		if ( text and string.find(text, LEVEL, 1, true) ) then
			local plainStart, plainEnd = string.find(text, nativeClass, 1, true);
			if ( plainStart ) then
				fs:SetText(text:sub(1, plainStart - 1) .. label .. text:sub(plainEnd + 1));
				break;
			end
		end
	end
	tooltip:Show();
end

GameTooltip:HookScript("OnTooltipSetUnit", function(self)
	local _, unit = self:GetUnit();
	if ( unit and UnitIsPlayer(unit) ) then
		ApplyTooltipLabel(self, unit);
	end
end);

-- Surface #3: peer refresh. A "peer" reply can land after the tooltip is already showing the
-- neutral "Adventurer" placeholder. ApplyTooltipLabel's splice already removed nativeClass from
-- the displayed text on the first pass, so re-running it here would find nothing to replace.
-- Instead, force the stock tooltip to rebuild for the same unit: SetUnit() regenerates the
-- native lines (nativeClass present again) and re-fires OnTooltipSetUnit, whose hook calls
-- ApplyTooltipLabel with the now-cached label. That one rebuild does not loop back here, since
-- SetUnit never triggers a "peer" addon message / FireUpdate.
MulticlassIdentity.OnUpdate(function()
	if ( GameTooltip:IsShown() ) then
		local _, unit = GameTooltip:GetUnit();
		if ( unit and UnitIsPlayer(unit) ) then
			GameTooltip:SetUnit(unit);
		end
	end
end);

-- Surface #4: /who results. WhoList_Update (FriendsFrame.lua) maps each visible row to a who-list
-- index via FauxScrollFrame_GetOffset(WhoListScrollFrame) + a 1..WHOS_TO_DISPLAY loop offset;
-- replicate that mapping to read the authoritative name, then overwrite the Class column, which is
-- its own FontString (plain SetText, no combined string).
local function ApplyWhoListLabels()
	local whoOffset = FauxScrollFrame_GetOffset(WhoListScrollFrame);
	for i = 1, WHOS_TO_DISPLAY do
		local name = GetWhoInfo(whoOffset + i);
		if ( name ) then
			local label = MulticlassIdentity.GetColoredLabel(name);
			if ( label ) then
				_G["WhoFrameButton"..i.."Class"]:SetText(label);
			end
		end
	end
end

hooksecurefunc("WhoList_Update", ApplyWhoListLabels);

-- Surfaces #5 and #6 both live in GuildStatus_Update (FriendsFrame.lua), called with no arguments;
-- hooksecurefunc hands our callbacks that same empty argument list, so each re-derives its data
-- straight from the roster API instead of reading the original call's locals.

-- Surface #5: guild roster Player-Status Class column. Only populated while viewing the
-- "player status" sub-view (FriendsFrame.playerStatusFrame); the sibling "guild status" sub-view
-- reuses different button names and has no class cell at all.
local function ApplyGuildRosterLabels()
	if ( not FriendsFrame.playerStatusFrame ) then
		return;
	end
	local guildOffset = FauxScrollFrame_GetOffset(GuildListScrollFrame);
	for i = 1, GUILDMEMBERS_TO_DISPLAY do
		local name = GetGuildRosterInfo(guildOffset + i);
		if ( name ) then
			local label = MulticlassIdentity.GetColoredLabel(name);
			if ( label ) then
				_G["GuildFrameButton"..i.."Class"]:SetText(label);
			end
		end
	end
end

hooksecurefunc("GuildStatus_Update", ApplyGuildRosterLabels);

-- Surface #6: guild member detail pane. GuildMemberDetailLevel is one combined "Level X Class"
-- string (SetFormattedText(FRIENDS_LEVEL_TEMPLATE, level, class)), populated only while a roster
-- row is selected; splice the native class substring out of the already-rendered text rather than
-- reconstructing the (locale-dependent) template ourselves. The name never appears in this string,
-- so there is no risk of the native class text coincidentally matching inside it.
local function ApplyGuildDetailLabel()
	if ( GetGuildRosterSelection() <= 0 ) then
		return;
	end
	local name, _, _, _, class = GetGuildRosterInfo(GetGuildRosterSelection());
	if ( not (name and class) ) then
		return;
	end
	local label = MulticlassIdentity.GetColoredLabel(name);
	if ( not label ) then
		return;
	end
	local text = GuildMemberDetailLevel:GetText();
	if ( not text ) then
		return;
	end
	local plainStart, plainEnd = string.find(text, class, 1, true);
	if ( plainStart ) then
		GuildMemberDetailLevel:SetText(text:sub(1, plainStart - 1) .. label .. text:sub(plainEnd + 1));
	end
end

hooksecurefunc("GuildStatus_Update", ApplyGuildDetailLabel);

-- Surface #7: friends-list online row. FriendsFrame_SetButton (FriendsFrame.lua) is the
-- DynamicScrollFrame's per-button renderer - it is already called once per visible row, so there is
-- no outer loop to replicate. It stamps buttonType/id onto the button itself, so read those back
-- rather than the file-local FriendButtons table that produced them. nameText is one combined
-- "Name, Level X Class" string; search for the native class only after the name portion, since the
-- name is embedded in the very same string and could otherwise coincidentally match first.
local function ApplyFriendsRowLabel(button)
	if ( button.buttonType ~= FRIENDS_BUTTON_TYPE_WOW ) then
		return;
	end
	local name, _, class, _, connected = GetFriendInfo(button.id);
	if ( not (connected and name and class) ) then
		return;
	end
	local label = MulticlassIdentity.GetColoredLabel(name);
	if ( not label ) then
		return;
	end
	local text = button.name:GetText();
	if ( not text ) then
		return;
	end
	local plainStart, plainEnd = string.find(text, class, #name + 1, true);
	if ( plainStart ) then
		button.name:SetText(text:sub(1, plainStart - 1) .. label .. text:sub(plainEnd + 1));
	end
end

hooksecurefunc("FriendsFrame_SetButton", ApplyFriendsRowLabel);

-- Surface #8: friends-list hover tooltip. FriendsFrameTooltip_Show (FriendsFrame.lua) is the actual
-- tooltip builder - FriendsFrame_SetButton only calls it incidentally, to keep an already-open
-- tooltip in sync - so hook it directly and the label lands however the tooltip got (re)built.
-- FriendsTooltipToon1Name holds only "Level X Class" (the name is a separate FontString), so there
-- is no name/class collision to guard against here.
local function ApplyFriendsTooltipLabel(self)
	if ( self.buttonType ~= FRIENDS_BUTTON_TYPE_WOW ) then
		return;
	end
	local name, _, class, _, connected = GetFriendInfo(self.id);
	if ( not (connected and name and class) ) then
		return;
	end
	local label = MulticlassIdentity.GetColoredLabel(name);
	if ( not label ) then
		return;
	end
	local text = FriendsTooltipToon1Name:GetText();
	if ( not text ) then
		return;
	end
	local plainStart, plainEnd = string.find(text, class, 1, true);
	if ( plainStart ) then
		FriendsTooltipToon1Name:SetText(text:sub(1, plainStart - 1) .. label .. text:sub(plainEnd + 1));
	end
end

hooksecurefunc("FriendsFrameTooltip_Show", ApplyFriendsTooltipLabel);

-- Peer refresh for surfaces #4-#8. WhoList_Update and GuildStatus_Update are NOT safe to call from
-- here: both unconditionally call the protected ShowUIPanel(FriendsFrame), and this refresher fires
-- off the insecure CHAT_MSG_ADDON "peer" path, so calling either would taint that protected call in
-- combat. Call our own label-only helpers instead - each re-derives its data straight from the
-- roster API (see their definitions above) and only touches text/tooltip strings, so they are
-- taint-safe. FriendsList_Update has no such protected call (it early-returns whenever
-- FriendsListFrame isn't shown) and is what DynamicScrollFrame_Update(FriendsFrameFriendsScrollFrame)
-- itself calls back into per visible row - called here without the isScrollUpdate argument, so it
-- redraws every visible row unconditionally, which in turn re-fires FriendsFrameTooltip_Show for an
-- open tooltip. That one call therefore covers both the friends row (#7) and its tooltip (#8).
MulticlassIdentity.OnUpdate(function()
	if ( WhoFrame:IsShown() ) then
		ApplyWhoListLabels();
	end
	if ( GuildFrame:IsShown() ) then
		ApplyGuildRosterLabels();
		ApplyGuildDetailLabel();
	end
	if ( FriendsListFrame:IsShown() ) then
		FriendsList_Update();
	end
end);

-- Surface #11: arena team roster Class column. PVPTeamDetails_Update(id) is the roster renderer;
-- row i is hidden once i exceeds GetNumArenaTeamMembers(id, 1), so replicate that same cutoff
-- before trusting GetArenaTeamRosterInfo for row i's data.
local function ApplyArenaRosterLabels(id)
	local numMembers = GetNumArenaTeamMembers(id, 1);
	for i = 1, MAX_ARENA_TEAM_MEMBERS do
		if ( i <= numMembers ) then
			local name = GetArenaTeamRosterInfo(id, i);
			local label = name and MulticlassIdentity.GetColoredLabel(name);
			if ( label ) then
				_G["PVPTeamDetailsButton"..i.."ClassText"]:SetText(label);
			end
		end
	end
end

hooksecurefunc("PVPTeamDetails_Update", ApplyArenaRosterLabels);

-- Surface #12: arena roster row tooltip. The same PVPTeamDetails_Update loop stamps a combined
-- "Level X Class" string onto button.tooltip (button = PVPTeamDetailsButton<i>, the whole row -
-- distinct from the ClassText FontString above); LEVEL and the level number never embed a class
-- name, so the first plain match is always the real one.
local function ApplyArenaRosterTooltips(id)
	local numMembers = GetNumArenaTeamMembers(id, 1);
	for i = 1, MAX_ARENA_TEAM_MEMBERS do
		if ( i <= numMembers ) then
			local name, _, _, class = GetArenaTeamRosterInfo(id, i);
			local button = _G["PVPTeamDetailsButton"..i];
			local label = (name and class and button.tooltip) and MulticlassIdentity.GetColoredLabel(name);
			if ( label ) then
				local plainStart, plainEnd = string.find(button.tooltip, class, 1, true);
				if ( plainStart ) then
					button.tooltip = button.tooltip:sub(1, plainStart - 1) .. label ..
						button.tooltip:sub(plainEnd + 1);
				end
			end
		end
	end
end

hooksecurefunc("PVPTeamDetails_Update", ApplyArenaRosterTooltips);

-- Surface #9: battlefield/arena scoreboard class tooltips. WorldStateScoreFrame_Update is the
-- scoreboard renderer, but on a just-ended match it also calls the protected ShowUIPanel, so the
-- peer refresher below calls this helper directly instead (see that refresher's comment). Two
-- tooltip strings carry the native class per row: the Name cell's "Race Class" tooltip and the
-- Class icon's plain "Class" tooltip; rewrite both from the same authoritative
-- GetBattlefieldScore(index) row.
local function ApplyBattlefieldScoreboardTooltips()
	local numScores = GetNumBattlefieldScores();
	local offset = FauxScrollFrame_GetOffset(WorldStateScoreScrollFrame);
	for i = 1, MAX_WORLDSTATE_SCORE_BUTTONS do
		local index = offset + i;
		if ( index <= numScores ) then
			local name, _, _, _, _, _, _, race, class = GetBattlefieldScore(index);
			local label = (name and race and class and class ~= "") and MulticlassIdentity.GetColoredLabel(name);
			if ( label ) then
				local nameFrame = _G["WorldStateScoreButton"..i.."Name"];
				local plainStart, plainEnd;
				if ( nameFrame and nameFrame.tooltip ) then
					plainStart, plainEnd = string.find(nameFrame.tooltip, class, #race + 1, true);
				end
				if ( plainStart ) then
					nameFrame.tooltip = nameFrame.tooltip:sub(1, plainStart - 1) .. label ..
						nameFrame.tooltip:sub(plainEnd + 1);
				end
				local classButton = _G["WorldStateScoreButton"..i.."ClassButton"];
				if ( classButton and classButton.tooltip == class ) then
					classButton.tooltip = label;
				end
			end
		end
	end
end

hooksecurefunc("WorldStateScoreFrame_Update", ApplyBattlefieldScoreboardTooltips);

-- Peer refresh for surfaces #9, #11, #12. Neither stock update is safe to call from here:
-- PVPTeamDetails_Update needs the currently displayed team id (PVPTeamDetails.team, the same field
-- the stock code itself reads before calling it), and WorldStateScoreFrame_Update can invoke the
-- protected ShowUIPanel when a match has just ended - tainting this insecure peer-message path.
-- Both helpers above only re-stamp text/tooltip strings, so calling them directly is taint-safe.
MulticlassIdentity.OnUpdate(function()
	if ( PVPTeamDetails:IsShown() and PVPTeamDetails.team ) then
		ApplyArenaRosterLabels(PVPTeamDetails.team);
		ApplyArenaRosterTooltips(PVPTeamDetails.team);
	end
	if ( WorldStateScoreFrame:IsShown() ) then
		ApplyBattlefieldScoreboardTooltips();
	end
end);

-- Surface #10: inspect sheet level line (other player). InspectPaperDollFrame_SetLevel reads
-- InspectFrame.unit and formats InspectLevelText from PLAYER_LEVEL ("Level %s %s %s" = level, race,
-- class); re-derive the whole line rather than splice, exactly like Surface #1's own-sheet hook, so
-- a later peer refresh upgrades the label cleanly instead of hunting for already-replaced text.
local function ApplyInspectLevelLabel()
	local unit = InspectFrame and InspectFrame.unit;
	if ( not unit ) then
		return;
	end
	local label = MulticlassIdentity.GetColoredLabel(UnitName(unit));
	if ( not label ) then
		return;
	end
	local level = UnitLevel(unit);
	if ( level == -1 ) then
		level = "??";
	end
	InspectLevelText:SetFormattedText(PLAYER_LEVEL, level, UnitRace(unit), label);
end

-- Surface #13: raid roster Class column. RaidGroupFrame_Update (Blizzard_RaidUI.lua) is the roster
-- renderer; row i is hidden once i exceeds GetNumRaidMembers(), so replicate that same cutoff before
-- trusting GetRaidRosterInfo for row i's data. Index i maps 1:1 onto RaidGroupButton<i> here (the
-- roster index IS the button number, unlike the scroll-offset surfaces elsewhere in this file).
local function ApplyRaidRosterLabels()
	local numRaidMembers = GetNumRaidMembers();
	for i = 1, MAX_RAID_MEMBERS do
		if ( i <= numRaidMembers ) then
			local name = GetRaidRosterInfo(i);
			local label = name and MulticlassIdentity.GetColoredLabel(name);
			if ( label ) then
				_G["RaidGroupButton"..i.."Class"]:SetText(label);
			end
		end
	end
end

-- Surface #14: calendar invite-list Class column. CalendarViewEventInviteListScrollFrame_Update
-- (Blizzard_Calendar.lua) is the invite-list renderer; it walks a HybridScrollFrame's visible
-- buttons, so replicate its offset math (HybridScrollFrame_GetOffset) rather than a plain 1..N loop.
local function ApplyCalendarInviteLabels()
	local buttons = CalendarViewEventInviteListScrollFrame.buttons;
	if ( not buttons ) then
		return;
	end
	local offset = HybridScrollFrame_GetOffset(CalendarViewEventInviteListScrollFrame);
	for i = 1, #buttons do
		local name = CalendarEventGetInvite(i + offset);
		local label = name and MulticlassIdentity.GetColoredLabel(name);
		if ( label ) then
			_G[buttons[i]:GetName().."Class"]:SetText(label);
		end
	end
end

-- Surface #14b: calendar CREATE/edit-event invite-list Class column.
-- CalendarCreateEventInviteListScrollFrame_Update is a separate renderer from surface #14's
-- CalendarViewEventInviteListScrollFrame_Update (the comment above the stock function literally
-- calls the two lists un-unified), with its own HybridScrollFrame - mirror the same offset math
-- against CalendarCreateEventInviteListScrollFrame instead. (The other "Class" global in that file,
-- CalendarEventInviteList_AnchorSortButtons's inviteClass local, only anchors the column's sort-header
-- button and is never itself a rendered class cell, so it needs no hook.)
local function ApplyCalendarCreateInviteLabels()
	local buttons = CalendarCreateEventInviteListScrollFrame.buttons;
	if ( not buttons ) then
		return;
	end
	local offset = HybridScrollFrame_GetOffset(CalendarCreateEventInviteListScrollFrame);
	for i = 1, #buttons do
		local name = CalendarEventGetInvite(i + offset);
		local label = name and MulticlassIdentity.GetColoredLabel(name);
		if ( label ) then
			_G[buttons[i]:GetName().."Class"]:SetText(label);
		end
	end
end

-- Surfaces #10, #13, #14, #14b above all live in load-on-demand Blizzard addons (Blizzard_InspectUI,
-- Blizzard_RaidUI, Blizzard_Calendar - none listed in FrameXML.toc), so none of their globals
-- (InspectPaperDollFrame_SetLevel, RaidGroupFrame_Update, CalendarViewEventInviteListScrollFrame_Update,
-- CalendarCreateEventInviteListScrollFrame_Update) exist yet when this file loads; hook each only once
-- its own ADDON_LOADED fires. A per-addon "hooked" flag guards against the same addon loading twice
-- (e.g. a /reload) and double-hooking.
local loadOnDemandHooked = {};

local function HookBlizzard_InspectUI()
	if ( loadOnDemandHooked.Blizzard_InspectUI ) then
		return;
	end
	loadOnDemandHooked.Blizzard_InspectUI = true;
	hooksecurefunc("InspectPaperDollFrame_SetLevel", ApplyInspectLevelLabel);
end

local function HookBlizzard_RaidUI()
	if ( loadOnDemandHooked.Blizzard_RaidUI ) then
		return;
	end
	loadOnDemandHooked.Blizzard_RaidUI = true;
	hooksecurefunc("RaidGroupFrame_Update", ApplyRaidRosterLabels);
end

local function HookBlizzard_Calendar()
	if ( loadOnDemandHooked.Blizzard_Calendar ) then
		return;
	end
	loadOnDemandHooked.Blizzard_Calendar = true;
	hooksecurefunc("CalendarViewEventInviteListScrollFrame_Update", ApplyCalendarInviteLabels);
	hooksecurefunc("CalendarCreateEventInviteListScrollFrame_Update", ApplyCalendarCreateInviteLabels);
end

local loadOnDemandFrame = CreateFrame("Frame");
loadOnDemandFrame:RegisterEvent("ADDON_LOADED");
loadOnDemandFrame:SetScript("OnEvent", function(self, event, addOnName)
	if ( addOnName == "Blizzard_InspectUI" ) then
		HookBlizzard_InspectUI();
	elseif ( addOnName == "Blizzard_RaidUI" ) then
		HookBlizzard_RaidUI();
	elseif ( addOnName == "Blizzard_Calendar" ) then
		HookBlizzard_Calendar();
	end
end);

-- Defensive only: FrameXML always loads before any load-on-demand addon, so in practice every
-- ADDON_LOADED above fires normally after this file is already loaded; guard anyway in case load
-- order ever changes and one of these three addons is already loaded by the time we get here.
if ( IsAddOnLoaded("Blizzard_InspectUI") ) then
	HookBlizzard_InspectUI();
end
if ( IsAddOnLoaded("Blizzard_RaidUI") ) then
	HookBlizzard_RaidUI();
end
if ( IsAddOnLoaded("Blizzard_Calendar") ) then
	HookBlizzard_Calendar();
end

-- Peer refresh for surfaces #10, #13, #14, #14b. Guard on loadOnDemandHooked first, since an addon
-- that never loaded has none of its frames/globals at all; only inspect's helper substantively
-- changes anything on a later peer resolve (raid/calendar just re-run the same column write,
-- harmless if the text was already correct).
MulticlassIdentity.OnUpdate(function()
	if ( loadOnDemandHooked.Blizzard_InspectUI and InspectFrame:IsShown() ) then
		ApplyInspectLevelLabel();
	end
	if ( loadOnDemandHooked.Blizzard_RaidUI and RaidFrame:IsShown() ) then
		ApplyRaidRosterLabels();
	end
	if ( loadOnDemandHooked.Blizzard_Calendar and CalendarViewEventFrame:IsShown() ) then
		ApplyCalendarInviteLabels();
	end
	if ( loadOnDemandHooked.Blizzard_Calendar and CalendarCreateEventFrame:IsShown() ) then
		ApplyCalendarCreateInviteLabels();
	end
end);

-- Surface #15: LFR (Raid Browser) results Class column. LFRBrowseFrameListButton_SetData(button,
-- index) is the per-row renderer, called once per visible row with its own button+index already in
-- hand, so unlike the scroll-frame surfaces above there is no offset math to replicate. Re-derive the
-- name from the same SearchLFGGetResults(index) call the stock code itself uses and overwrite
-- button.class, the row's plain Class FontString (no combined string, no splice needed).
local function ApplyLFRResultsLabel(button, index)
	local name = SearchLFGGetResults(index);
	if ( not name ) then
		return;
	end
	local label = MulticlassIdentity.GetColoredLabel(name);
	if ( label ) then
		button.class:SetText(label);
	end
end

hooksecurefunc("LFRBrowseFrameListButton_SetData", ApplyLFRResultsLabel);

-- Surface #16: LFR results row tooltip. LFRBrowseButton_OnEnter builds the whole GameTooltip from
-- scratch on hover; the "Level X Class" line (className, from that same SearchLFGGetResults(index)
-- call) is only added for an individual posting (partyMembers == 0) - a party posting's tooltip
-- instead lists ignored/friend sub-members via SearchLFGGetPartyResults with no class text at all, so
-- replicate that same branch condition rather than blindly searching every line (the poster's own
-- name line could otherwise coincidentally match a class word and get corrupted).
local function ApplyLFRResultsTooltipLabel(self)
	local name, _, _, className, _, partyMembers = SearchLFGGetResults(self.index);
	if ( partyMembers > 0 or not (name and className) ) then
		return;
	end
	local label = MulticlassIdentity.GetColoredLabel(name);
	if ( not label ) then
		return;
	end
	for i = 2, GameTooltip:NumLines() do   -- line 1 is always the poster name, never the class line
		local fs = _G["GameTooltipTextLeft"..i];
		local text = fs and fs:GetText();
		if ( text ) then
			local plainStart, plainEnd = string.find(text, className, 1, true);
			if ( plainStart ) then
				fs:SetText(text:sub(1, plainStart - 1) .. label .. text:sub(plainEnd + 1));
				break;
			end
		end
	end
	GameTooltip:Show();
end

hooksecurefunc("LFRBrowseButton_OnEnter", ApplyLFRResultsTooltipLabel);

-- Peer refresh for surfaces #15-#16. LFRBrowseFrameList_Update itself is not called here since it
-- also resets LFRBrowseFrameRefreshButton's auto-refresh countdown as a side effect; instead replicate
-- its per-row show/hide loop directly against the already-hooked per-row helper above.
-- LFRBrowseButton_OnEnter has no such side effect (it only builds/owns GameTooltip), so it is safe to
-- call again directly for whichever row's tooltip is currently open.
MulticlassIdentity.OnUpdate(function()
	if ( not LFRBrowseFrame:IsShown() ) then
		return;
	end
	for i = 1, NUM_LFR_LIST_BUTTONS do
		local button = _G["LFRBrowseFrameListButton"..i];
		if ( button:IsShown() ) then
			ApplyLFRResultsLabel(button, button.index);
			if ( GameTooltip:GetOwner() == button ) then
				LFRBrowseButton_OnEnter(button);
			end
		end
	end
end);
