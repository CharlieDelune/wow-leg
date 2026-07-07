-- Multiclass projection display: keep stock resource UI that is class-gated at UI init in sync when the
-- projected (client-visible) class changes live, without a full relog.
--
-- The stock RuneFrame decides its visibility once, in RuneFrame_OnLoad, from the login-time class and never
-- re-checks; activating Death Knight into slot 0 at runtime (or moving it out) would otherwise leave the
-- rune widget stale until relog. Runic power is exclusive to Death Knight, so the projected class's display
-- power is an exact signal: drive the rune widget off UNIT_DISPLAYPOWER, which the server's live power-type
-- flip fires. The server resyncs the rune state alongside that flip, so the buttons render current values.

local function UpdateRuneFrameForProjection()
	if ( not RuneFrame or not RuneFrame.runes ) then
		return;
	end
	if ( UnitPowerType("player") == SPELL_POWER_RUNIC_POWER ) then
		RuneFrame:Show();
		for i = 1, #RuneFrame.runes do
			RuneButton_Update(RuneFrame.runes[i], i);
		end
	else
		RuneFrame:Hide();
	end
end

local watcher = CreateFrame("Frame");
watcher:RegisterEvent("UNIT_DISPLAYPOWER");
watcher:SetScript("OnEvent", function(self, event, unit)
	if ( unit == "player" ) then
		UpdateRuneFrameForProjection();
	end
end);
