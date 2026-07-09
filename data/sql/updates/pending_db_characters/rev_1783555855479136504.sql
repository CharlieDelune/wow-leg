--
-- Multiclass Loadouts (Phase 1): named build configurations + active pointer.
--
ALTER TABLE `character_multiclass`
    ADD COLUMN `activeLoadoutId` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Currently active loadout id (0 = none/legacy)';

DROP TABLE IF EXISTS `character_multiclass_loadout`;
CREATE TABLE `character_multiclass_loadout` (
    `guid` INT UNSIGNED NOT NULL COMMENT 'Character low GUID',
    `loadoutId` INT UNSIGNED NOT NULL COMMENT 'Per-character loadout id',
    `name` VARCHAR(48) NOT NULL DEFAULT '' COMMENT 'Player-chosen loadout name',
    `description` VARCHAR(255) NOT NULL DEFAULT '' COMMENT 'Player-chosen free-text note',
    `icon` VARCHAR(128) NOT NULL DEFAULT '' COMMENT 'Macro-picker texture string (Phase 3 hotbar macro icon)',
    `sortOrder` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Display order',
    PRIMARY KEY (`guid`, `loadoutId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='multiclass loadouts: metadata';

DROP TABLE IF EXISTS `character_multiclass_loadout_slot`;
CREATE TABLE `character_multiclass_loadout_slot` (
    `guid` INT UNSIGNED NOT NULL,
    `loadoutId` INT UNSIGNED NOT NULL,
    `slot` TINYINT UNSIGNED NOT NULL,
    `classId` TINYINT UNSIGNED NOT NULL,
    PRIMARY KEY (`guid`, `loadoutId`, `slot`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='multiclass loadouts: arrangement snapshot';

DROP TABLE IF EXISTS `character_multiclass_loadout_talent`;
CREATE TABLE `character_multiclass_loadout_talent` (
    `guid` INT UNSIGNED NOT NULL,
    `loadoutId` INT UNSIGNED NOT NULL,
    `spell` INT UNSIGNED NOT NULL COMMENT 'Current-rank talent spellId; owning class derived from TalentTab.ClassMask',
    PRIMARY KEY (`guid`, `loadoutId`, `spell`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='multiclass loadouts: talent snapshot';

DROP TABLE IF EXISTS `character_multiclass_loadout_glyph`;
CREATE TABLE `character_multiclass_loadout_glyph` (
    `guid` INT UNSIGNED NOT NULL,
    `loadoutId` INT UNSIGNED NOT NULL,
    `classId` TINYINT UNSIGNED NOT NULL,
    `glyph1` INT UNSIGNED NOT NULL DEFAULT 0,
    `glyph2` INT UNSIGNED NOT NULL DEFAULT 0,
    `glyph3` INT UNSIGNED NOT NULL DEFAULT 0,
    `glyph4` INT UNSIGNED NOT NULL DEFAULT 0,
    `glyph5` INT UNSIGNED NOT NULL DEFAULT 0,
    `glyph6` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`guid`, `loadoutId`, `classId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='multiclass loadouts: per-class glyph snapshot';
