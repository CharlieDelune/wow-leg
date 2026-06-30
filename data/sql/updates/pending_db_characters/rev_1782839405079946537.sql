DROP TABLE IF EXISTS `character_multiclass_slot`;
CREATE TABLE `character_multiclass_slot` (
    `guid` INT UNSIGNED NOT NULL COMMENT 'Character low GUID',
    `slot` TINYINT UNSIGNED NOT NULL COMMENT 'Class slot index 0-2',
    `classId` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 = empty',
    `unlocked` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`guid`, `slot`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-multiclass: active class slots per character';

DROP TABLE IF EXISTS `character_multiclass_class`;
CREATE TABLE `character_multiclass_class` (
    `guid` INT UNSIGNED NOT NULL COMMENT 'Character low GUID',
    `classId` TINYINT UNSIGNED NOT NULL,
    `level` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `xp` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`guid`, `classId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-multiclass: per-class progression remembered across swaps';
