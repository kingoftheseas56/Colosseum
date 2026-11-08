import { URL, URLSearchParams } from 'whatwg-url';
import { Buffer } from 'buffer/';
globalThis.URL = URL;
globalThis.URLSearchParams = URLSearchParams;
globalThis.Buffer = Buffer;
globalThis.__vendorModules = {
  "cheerio-without-node-native": () => require("cheerio-without-node-native"),
  "crypto-js": () => require("crypto-js")
};
