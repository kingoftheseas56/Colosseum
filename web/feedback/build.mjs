import { cp, mkdir, readFile, writeFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import path from "node:path";
import { createHash } from "node:crypto";
import { validConfiguration } from "./public/feedback-core.mjs";
const root = fileURLToPath(new URL("../../", import.meta.url));
const source = fileURLToPath(new URL("./public/", import.meta.url));
const output = path.resolve(
  process.argv[2] || fileURLToPath(new URL("./dist/", import.meta.url)),
);
const config = JSON.parse(
  await readFile(path.join(source, "config.json"), "utf8"),
);
if (process.env.COLOSSEUM_FEEDBACK_FORM_ENDPOINT)
  config.formEndpoint = process.env.COLOSSEUM_FEEDBACK_FORM_ENDPOINT;
if (process.env.COLOSSEUM_FEEDBACK_PROVIDER)
  config.provider = process.env.COLOSSEUM_FEEDBACK_PROVIDER;
if (process.env.COLOSSEUM_FEEDBACK_ACCESS_KEY)
  config.accessKey = process.env.COLOSSEUM_FEEDBACK_ACCESS_KEY;
if (process.env.COLOSSEUM_FEEDBACK_TURNSTILE_SITE_KEY)
  config.turnstileSiteKey = process.env.COLOSSEUM_FEEDBACK_TURNSTILE_SITE_KEY;
if (!validConfiguration(config))
  throw new Error(
    "Configure a public Formspark form endpoint or Web3Forms access key. Private API tokens are not used.",
  );
await mkdir(output, { recursive: true });
await cp(source, output, { recursive: true });
// Version the module graph and stylesheet together so cached embedded browsers
// request the assets that belong to this HTML build.
const versionedName = (name, content) => {
  const hash = createHash("sha256").update(content).digest("hex").slice(0, 12);
  const extension = path.extname(name);
  return `${path.basename(name, extension)}.${hash}${extension}`;
};
const core = await readFile(path.join(output, "feedback-core.mjs"));
const coreName = versionedName("feedback-core.mjs", core);
await writeFile(path.join(output, coreName), core);
const main = (await readFile(path.join(output, "feedback.mjs"), "utf8"))
  .replace('"./feedback-core.mjs"', `"./${coreName}"`);
const mainName = versionedName("feedback.mjs", main);
await writeFile(path.join(output, mainName), main);
const style = await readFile(path.join(output, "style.css"));
const styleName = versionedName("style.css", style);
await writeFile(path.join(output, styleName), style);
const html = (await readFile(path.join(output, "index.html"), "utf8"))
  .replace('href="./style.css"', `href="./${styleName}"`)
  .replace('src="./feedback.mjs"', `src="./${mainName}"`);
await writeFile(path.join(output, "index.html"), html);
await mkdir(path.join(output, "assets"), { recursive: true });
await cp(
  path.join(root, "assets/fonts/Fraunces-Regular.ttf"),
  path.join(output, "assets/Fraunces-Regular.ttf"),
);
for (const icon of ["colosseum", "comic-book", "book-library", "projector-theatre", "universes-atom", "vault-folder"])
  await cp(path.join(root, `assets/icons/${icon}.svg`), path.join(output, `assets/${icon}.svg`));
await writeFile(
  path.join(output, "config.json"),
  JSON.stringify(config, null, 2) + "\n",
);
console.log(
  `Feedback page: ${output}${config.formEndpoint ? "" : " (submission endpoint pending)"}`,
);
