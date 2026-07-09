--
-- Loadouts Phase 2: permanent gold-bought loadout-slot ratchet (per character).
-- Effective loadout capacity = Multiclass.Loadout.FreeSlots (config) + purchasedLoadoutSlots.
--
ALTER TABLE `character_multiclass`
    ADD COLUMN `purchasedLoadoutSlots` INT UNSIGNED NOT NULL DEFAULT 0
    COMMENT 'Permanent gold-bought loadout-slot ratchet (Phase 2)' AFTER `activeLoadoutId`;
