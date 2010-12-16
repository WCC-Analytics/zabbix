ALTER TABLE sessions ALTER COLUMN userid SET WITH DEFAULT NULL
/
REORG TABLE sessions
/
DELETE FROM sessions WHERE NOT userid IN (SELECT userid FROM users)
/
ALTER TABLE sessions ADD CONSTRAINT c_sessions_1 FOREIGN KEY (userid) REFERENCES users (userid) ON DELETE CASCADE
/
REORG TABLE sessions
/
