DROP TABLE IF EXISTS `character_multiclass`;
CREATE TABLE `character_multiclass` (
    `guid` INT UNSIGNED NOT NULL COMMENT 'Character low GUID',
    `unlockedSlots` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'Earned active-slot capacity (progression ratchet)',
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='multiclass: per-character earned active-slot capacity';
