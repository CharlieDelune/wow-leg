-- Client-side expansion of the server's {mcU}/{mcL} diegetic markers on the WDB-cached narrative
-- surfaces (gossip greeting, quest description/objectives, book pages). The server emits these markers
-- globally so the shared client cache stays consistent; here they are expanded per-viewer: the realm's
-- configured word for a character with 2+ active classes, or the viewer's own class name (reproducing
-- $C/$c) for a single active class. Marker-only -- never matches a literal word, so prose is untouched.

local function LocalActiveCount()
	local st = MulticlassUI and MulticlassUI.state;
	if ( not st or not st.enable or not st.classes ) then
		return 0;
	end
	local n = 0;
	for _, class in ipairs(st.classes) do
		if ( class.slot and class.slot >= 0 ) then
			n = n + 1;
		end
	end
	return n;
end

local function ExpandMarkers(text)
	if ( not text or not string.find(text, "{mc", 1, true) ) then
		return text;
	end
	local multi = ( LocalActiveCount() >= 2 );
	local word = MulticlassUI and MulticlassUI.diegeticWord;
	return (string.gsub(text, "{mc([UL])}", function(case)
		if ( multi and word and word ~= "" ) then
			if ( case == "U" ) then
				return string.upper(string.sub(word, 1, 1)) .. string.sub(word, 2);
			end
			return word;
		end
		local className = UnitClass("player") or "";
		if ( case == "U" ) then
			return className;
		end
		return string.lower(className);
	end));
end

local function ExpandFontString(fontString)
	if ( fontString ) then
		local text = fontString:GetText();
		if ( text and string.find(text, "{mc", 1, true) ) then
			fontString:SetText(ExpandMarkers(text));
		end
	end
end

if ( type(GossipFrameUpdate) == "function" ) then
	hooksecurefunc("GossipFrameUpdate", function()
		ExpandFontString(GossipGreetingText);
	end);
end

if ( type(QuestInfo_Display) == "function" ) then
	hooksecurefunc("QuestInfo_Display", function()
		ExpandFontString(QuestInfoDescriptionText);
		ExpandFontString(QuestInfoObjectivesText);
	end);
end

if ( type(ItemTextFrame_OnEvent) == "function" ) then
	hooksecurefunc("ItemTextFrame_OnEvent", function()
		ExpandFontString(ItemTextPageText);
	end);
end
