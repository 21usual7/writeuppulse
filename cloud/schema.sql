-- One row per synced item of one account. `user` is the SHA-256 of the
-- client's auth token, `id` an HMAC the client made from its own key, `data`
-- AES-GCM ciphertext: nothing here says what the row is about.
CREATE TABLE IF NOT EXISTS rows (
    user TEXT NOT NULL,
    id TEXT NOT NULL,
    data TEXT NOT NULL,
    updated INTEGER NOT NULL,
    seq INTEGER NOT NULL,
    PRIMARY KEY (user, id)
);
CREATE INDEX IF NOT EXISTS rows_by_seq ON rows (user, seq);
