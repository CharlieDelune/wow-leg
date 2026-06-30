DROP TABLE IF EXISTS `character_multiclass_spell`;
CREATE TABLE `character_multiclass_spell` (
    `guid`    INT UNSIGNED     NOT NULL COMMENT 'Character low GUID',
    `classId` TINYINT UNSIGNED NOT NULL COMMENT 'Owning class for this learned spell',
    `spellId` INT UNSIGNED     NOT NULL COMMENT 'Learned spell ID (rank encoded by spell ID)',
    PRIMARY KEY (`guid`, `classId`, `spellId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-multiclass: per-class learned-spell ledger, remembered across swaps';
