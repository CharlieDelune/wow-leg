--
-- P3a: per-class talent-reset ladder state (last cost paid + last reset time), one pair per owned class.
-- Additive, defaulted to 0 (== "never reset"), so pre-existing rows self-heal with no data migration.
--
ALTER TABLE `character_multiclass_class`
    ADD COLUMN `talentResetCost` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Last talent reset cost paid (copper); 0 = never' AFTER `xp`,
    ADD COLUMN `talentResetTime` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Unixtime of last talent reset; 0 = never' AFTER `talentResetCost`;
