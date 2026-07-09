--
-- Loadouts Phase 3: opaque per-character client UI state for the quick-switch bar
-- (shown / columns / scale / position). The server round-trips this verbatim; it never parses it.
--
ALTER TABLE `character_multiclass`
    ADD COLUMN `loadoutBarPrefs` VARCHAR(64) NOT NULL DEFAULT ''
    COMMENT 'Opaque client UI state for the loadout quick-switch bar (Phase 3)' AFTER `purchasedLoadoutSlots`;
