// WriteupPulse sync: one endpoint, POST /sync, with
//   Authorization: Bearer <64 hex chars>   (derived from the user's sync key)
//   {"since": <seq>, "rows": [{"id", "data", "updated"}]}
// It stores the rows (a row only replaces an older one: last write wins by
// `updated`, the time of the change on the user's computer) and answers with
// the account's rows changed after `since`: {"rows": [{id, data, updated, seq}],
// "more": bool}. The rows are encrypted by the app; this never sees a key.

const MAX_BODY = 4 * 1024 * 1024;
const MAX_ROWS_IN = 500;
const MAX_ROWS_OUT = 1000;
const MAX_DATA = 64 * 1024;
const MAX_ROWS_PER_USER = 50000;

const json = (body, status = 200) =>
    new Response(JSON.stringify(body), { status, headers: { "content-type": "application/json" } });

async function sha256(text) {
    const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(text));
    return [...new Uint8Array(digest)].map(b => b.toString(16).padStart(2, "0")).join("");
}

export default {
    async fetch(request, env) {
        const url = new URL(request.url);
        if (url.pathname !== "/sync")
            return json({ error: "not found" }, 404);
        if (request.method !== "POST")
            return json({ error: "use POST" }, 405);
        const auth = /^Bearer ([0-9a-f]{64})$/.exec(request.headers.get("authorization") || "");
        if (!auth)
            return json({ error: "no sync key" }, 401);
        if (Number(request.headers.get("content-length") || 0) > MAX_BODY)
            return json({ error: "too large" }, 413);
        const text = await request.text();
        if (text.length > MAX_BODY)
            return json({ error: "too large" }, 413);
        let body;
        try { body = JSON.parse(text); } catch { return json({ error: "not JSON" }, 400); }
        const since = Number.isSafeInteger(body.since) && body.since >= 0 ? body.since : 0;
        const rows = Array.isArray(body.rows) ? body.rows : [];
        if (rows.length > MAX_ROWS_IN)
            return json({ error: "too many rows" }, 413);
        const soon = Date.now() + 24 * 3600 * 1000;
        for (const row of rows)
            if (!row || typeof row.id !== "string" || !/^[0-9a-f]{64}$/.test(row.id)
                || typeof row.data !== "string" || row.data.length > MAX_DATA || !/^[A-Za-z0-9+/=]+$/.test(row.data)
                || !Number.isSafeInteger(row.updated) || row.updated < 0 || row.updated > soon)
                return json({ error: "bad row" }, 400);

        const user = await sha256(auth[1]);
        if (rows.length) {
            const count = await env.DB.prepare("SELECT count(*) AS n FROM rows WHERE user = ?").bind(user).first("n");
            if (count + rows.length > MAX_ROWS_PER_USER)
                return json({ error: "account full" }, 507);
            // Each statement takes the next number in the account, in order,
            // inside the batch's one transaction.
            await env.DB.batch(rows.map(row => env.DB.prepare(
                "INSERT INTO rows (user, id, data, updated, seq)"
                + " VALUES (?1, ?2, ?3, ?4, (SELECT coalesce(max(seq), 0) + 1 FROM rows WHERE user = ?1))"
                + " ON CONFLICT (user, id) DO UPDATE SET data = excluded.data, updated = excluded.updated,"
                + " seq = excluded.seq WHERE excluded.updated > rows.updated"
            ).bind(user, row.id, row.data, row.updated)));
        }
        const { results } = await env.DB.prepare(
            "SELECT id, data, updated, seq FROM rows WHERE user = ? AND seq > ? ORDER BY seq LIMIT ?"
        ).bind(user, since, MAX_ROWS_OUT + 1).all();
        return json({ rows: results.slice(0, MAX_ROWS_OUT), more: results.length > MAX_ROWS_OUT });
    },
};
