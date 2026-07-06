MulticlassIdentity = MulticlassIdentity or {};

local PREFIX = "MCLS";
local FLUSH_INTERVAL = 0.1;
local MAX_PAYLOAD = 240;
local NEUTRAL_HEX = "808080";

local CLASS_TOKEN = { [1] = "WARRIOR", [2] = "PALADIN", [3] = "HUNTER", [4] = "ROGUE", [5] = "PRIEST",
                       [6] = "DEATHKNIGHT", [7] = "SHAMAN", [8] = "MAGE", [9] = "WARLOCK", [11] = "DRUID" };

-- Canonical, context-independent class abbreviations (see MulticlassFrame.lua for the design rationale).
-- Kept as this module's own copy: MulticlassFrame.lua still needs CLASS_TOKEN/ClassName/ClassColor for
-- tile rendering, unrelated to label building, so nothing is shared across the two files.
local CLASS_ABBR3 = { [1] = "War", [2] = "Pld", [3] = "Hnt", [4] = "Rog", [5] = "Prs",
                       [6] = "DK", [7] = "Shm", [8] = "Mag", [9] = "Wlk", [11] = "Drd" };
local CLASS_ABBR2 = { [1] = "W", [2] = "P", [3] = "H", [4] = "R", [5] = "Pr",
                       [6] = "DK", [7] = "S", [8] = "M", [9] = "Wl", [11] = "D" };

local function ClassName(id)
	return LOCALIZED_CLASS_NAMES_MALE[CLASS_TOKEN[id]] or ("Class " .. id);
end

local function ClassColor(id)
	return RAID_CLASS_COLORS[CLASS_TOKEN[id]] or { r = 1, g = 1, b = 1 };
end

local function Abbrev(id, width)
	local hard = (width == 3) and CLASS_ABBR3 or CLASS_ABBR2;
	if ( hard[id] ) then
		return hard[id];
	end
	local letters = ClassName(id):gsub("%s+", ""):sub(1, width);   -- fallback: truncate the name
	return letters:sub(1, 1):upper() .. letters:sub(2):lower();
end

-- Composite identity label. full @1 | 3-char @2-3 | short @4-5 | Adventurer @6+.
function MulticlassIdentity.PlainLabel(ids)
	local n = #ids;
	if ( n == 0 ) then return "" end;
	if ( n == 1 ) then return ClassName(ids[1]) end;
	if ( n >= 6 ) then return "Adventurer" end;
	local width = (n <= 3) and 3 or 2;
	local parts = {};
	for _, id in ipairs(ids) do
		table.insert(parts, Abbrev(id, width));
	end
	return table.concat(parts, "/");
end

-- Same scheme, each abbreviation in its class color and separators dimmed. A single class stays
-- byte-vanilla (plain native name, no color); 6+ collapses to a neutral-colored "Adventurer".
function MulticlassIdentity.ColorLabel(ids)
	local n = #ids;
	if ( n == 0 ) then return "" end;
	if ( n == 1 ) then return ClassName(ids[1]) end;
	if ( n >= 6 ) then return "|cff" .. NEUTRAL_HEX .. "Adventurer|r" end;
	local width = (n <= 3) and 3 or 2;
	local parts = {};
	for _, id in ipairs(ids) do
		local c = ClassColor(id);
		local hex = string.format("%02x%02x%02x", c.r * 255, c.g * 255, c.b * 255);
		table.insert(parts, "|cff" .. hex .. Abbrev(id, width) .. "|r");
	end
	return table.concat(parts, "|cff" .. NEUTRAL_HEX .. "/|r");
end

-- Name-keyed peer cache; cache[name] = { ids = { classId, ... } }. An entry with an empty ids list
-- means "asked and got nothing back" (peer not tracked) - cached so we stop re-querying it.
local cache = {};
local inflight = {};
local queued = {};
local queuedSet = {};
local callbacks = {};

local function FireUpdate()
	for _, fn in ipairs(callbacks) do
		fn();
	end
end

function MulticlassIdentity.OnUpdate(fn)
	table.insert(callbacks, fn);
end

-- Active classIds in slot order, derived the same way MulticlassFrame's BuildView does.
local function SelfActiveIds()
	local st = MulticlassUI and MulticlassUI.state;
	if ( not st ) then
		return {};
	end
	local active = {};
	for _, c in ipairs(st.classes) do
		if ( c.slot and c.slot >= 0 ) then
			table.insert(active, c);
		end
	end
	table.sort(active, function(a, b) return a.slot < b.slot end);
	local ids = {};
	for _, c in ipairs(active) do
		table.insert(ids, c.id);
	end
	return ids;
end

local function QueueWhois(name)
	if ( cache[name] or inflight[name] or queuedSet[name] ) then
		return;
	end
	inflight[name] = true;
	queuedSet[name] = true;
	table.insert(queued, name);
end

local function FlushQueued()
	if ( #queued == 0 ) then
		return;
	end
	local base = "whois ";
	local chunk = base;
	for _, name in ipairs(queued) do
		local candidate = (chunk == base) and (chunk .. name) or (chunk .. " " .. name);
		if ( #candidate > MAX_PAYLOAD and chunk ~= base ) then
			SendAddonMessage(PREFIX, chunk, "WHISPER", UnitName("player"));
			chunk = base .. name;
		else
			chunk = candidate;
		end
	end
	if ( chunk ~= base ) then
		SendAddonMessage(PREFIX, chunk, "WHISPER", UnitName("player"));
	end
	queued = {};
	queuedSet = {};
end

-- Cached colored label for a player name; nil when the feature is off (caller keeps the native text).
function MulticlassIdentity.GetColoredLabel(name)
	if ( not (MulticlassUI and MulticlassUI.state and MulticlassUI.state.enable) ) then
		return nil;
	end
	if ( name == UnitName("player") ) then
		return MulticlassIdentity.ColorLabel(SelfActiveIds());
	end
	local entry = cache[name];
	if ( entry and #entry.ids > 0 ) then
		return MulticlassIdentity.ColorLabel(entry.ids);
	end
	if ( not entry ) then
		QueueWhois(name);
	end
	return "|cff" .. NEUTRAL_HEX .. "Adventurer|r";
end

-- Parse "peer <name> <id id ...>" (empty id list = asked-and-unknown; still cached, see above).
local function OnPeer(message)
	local it = string.gmatch(message, "%S+");
	it();                               -- "peer"
	local name = it();
	if ( not name ) then
		return;
	end
	local ids = {};
	for tok in it do
		local id = tonumber(tok);
		if ( id ) then
			table.insert(ids, id);
		end
	end
	cache[name] = { ids = ids };
	inflight[name] = nil;
	FireUpdate();
end

local commsFrame = CreateFrame("Frame");
commsFrame:RegisterEvent("CHAT_MSG_ADDON");
commsFrame:SetScript("OnEvent", function(self, event, ...)
	local prefix, message = ...;
	if ( prefix ~= PREFIX ) then
		return;
	end
	local verb = string.match(message, "^(%S+)");
	if ( verb == "peer" ) then
		OnPeer(message);
	end
end);

local sinceFlush = 0;
commsFrame:SetScript("OnUpdate", function(self, elapsed)
	sinceFlush = sinceFlush + elapsed;
	if ( sinceFlush < FLUSH_INTERVAL ) then
		return;
	end
	sinceFlush = 0;
	FlushQueued();
end);
