export function wikiArticle(page) {
  if (!Number.isSafeInteger(page?.pageid) || page.pageid < 1 ||
      typeof page.title !== "string" || !page.title.trim() || page.title.length > 120)
    return null;
  return {
    pageid: page.pageid,
    title: page.title.trim(),
    extract: typeof page.extract === "string"
      ? page.extract.replace(/\s+/g, " ").trim().slice(0, 160) : "",
  };
}

export async function searchWikiArticles(query, signal) {
  const params = new URLSearchParams({
    action: "query", generator: "search", gsrsearch: query,
    gsrnamespace: "0", gsrlimit: "4", prop: "extracts|pageprops",
    ppprop: "disambiguation", explaintext: "1", exintro: "1",
    exchars: "160", exlimit: "4", format: "json", formatversion: "2", origin: "*",
  });
  const response = await fetch(`https://en.wikipedia.org/w/api.php?${params}`, {
    credentials: "omit",
    headers: { "Api-User-Agent": "ColosseumUniverseRequest/1.0 (https://getcolosseum.world/)" },
    signal,
  });
  if (!response.ok) throw new Error();
  const data = await response.json();
  if (data.error) throw new Error();
  return (data.query?.pages || [])
    .filter(page => !page.missing && !Object.hasOwn(page.pageprops || {}, "disambiguation"))
    .sort((a, b) => a.index - b.index)
    .map(wikiArticle).filter(Boolean);
}
