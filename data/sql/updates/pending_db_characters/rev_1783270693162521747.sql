-- Multiclass per-character tables (consolidated). Custom tables with no vanilla seed data, so the P3a
-- talent-reset-ladder columns are baked into character_multiclass_class instead of added by a later ALTER.
DROP TABLE IF EXISTS `character_multiclass`;
CREATE TABLE `character_multiclass` (
    `guid` INT UNSIGNED NOT NULL COMMENT 'Character low GUID',
    `unlockedSlots` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'Earned active-slot capacity (progression ratchet)',
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='multiclass: per-character earned active-slot capacity';

DROP TABLE IF EXISTS `character_multiclass_slot`;
CREATE TABLE `character_multiclass_slot` (
    `guid` INT UNSIGNED NOT NULL COMMENT 'Character low GUID',
    `slot` TINYINT UNSIGNED NOT NULL COMMENT 'Class slot index 0-2',
    `classId` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 = empty',
    `unlocked` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`guid`, `slot`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='multiclass: active class slots per character';

DROP TABLE IF EXISTS `character_multiclass_class`;
CREATE TABLE `character_multiclass_class` (
    `guid` INT UNSIGNED NOT NULL COMMENT 'Character low GUID',
    `classId` TINYINT UNSIGNED NOT NULL,
    `level` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `xp` INT UNSIGNED NOT NULL DEFAULT 0,
    `talentResetCost` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Last talent reset cost paid (copper); 0 = never',
    `talentResetTime` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Unixtime of last talent reset; 0 = never',
    PRIMARY KEY (`guid`, `classId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='multiclass: per-class progression remembered across swaps';

DROP TABLE IF EXISTS `character_multiclass_spell`;
CREATE TABLE `character_multiclass_spell` (
    `guid` INT UNSIGNED NOT NULL COMMENT 'Character low GUID',
    `classId` TINYINT UNSIGNED NOT NULL COMMENT 'Owning class for this learned spell',
    `spellId` INT UNSIGNED NOT NULL COMMENT 'Learned spell ID (rank encoded by spell ID)',
    PRIMARY KEY (`guid`, `classId`, `spellId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='multiclass: per-class learned-spell ledger, remembered across swaps';
