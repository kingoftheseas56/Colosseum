export const DRAFT_KEY = "colosseum-feedback-v1";
export function validEndpoint(value) {
  return (
    typeof value === "string" &&
    /^https:\/\/submit-form\.com\/[A-Za-z0-9_-]+$/.test(value)
  );
}
export function validConfiguration(config) {
  if (config.provider === "formspark")
    return validEndpoint(config.formEndpoint);
  if (config.provider === "web3forms")
    return (
      /^[a-f\d]{8}(?:-[a-f\d]{4}){3}-[a-f\d]{12}$/i.test(
        config.accessKey || "",
      ) && !config.turnstileSiteKey
    );
  return false;
}
export function environment(search) {
  const p = new URLSearchParams(search);
  const platforms = {
    windows: "Windows",
    android: "Android",
    linux: "Linux",
    osx: "macOS",
  };
  const version = (p.get("version") || "")
    .replace(/[^\w.+-]/g, "")
    .slice(0, 64);
  return { platform: platforms[p.get("platform")] || "Web", version };
}
export function validate(data) {
  const errors = {};
  if (!data.title.trim()) errors.title = "Give your message a short title.";
  if (data.title.length > 120)
    errors.title = "Keep the title to 120 characters.";
  if (data.message.trim().length < 20)
    errors.message = "Add a little more detail (at least 20 characters).";
  if (data.message.length > 6000)
    errors.message = "Keep your message to 6,000 characters.";
  if (
    data.email &&
    (data.email.length > 254 || !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(data.email))
  )
    errors.email = "Enter a valid email address, or leave it empty.";
  return errors;
}
export function sendError(status) {
  if (status === 402)
    return "Our feedback inbox has reached its capacity. Your draft is safe here; please try again later.";
  if (status === 403)
    return "Your message was not accepted by the spam check. Check the verification and try again.";
  if (status === 429)
    return "Too many attempts. Wait a moment, then try again.";
  return "Your message could not be sent. Your draft is safe here; please try again.";
}
export async function sendFeedback(
  config,
  payload,
  fetcher = fetch,
  timeoutMs = 20000,
) {
  if (!validConfiguration(config))
    throw new Error("Feedback is being connected. Please try again later.");
  const web3 = config.provider === "web3forms";
  const endpoint = web3
    ? "https://api.web3forms.com/submit"
    : config.formEndpoint;
  const subject =
    `[Colosseum] [${payload.type}] [${payload.area || "General"}] ${payload.title}`.replace(
      /[\r\n]/g,
      " ",
    );
  const body = web3
    ? {
        ...payload,
        access_key: config.accessKey,
        subject,
        from_name: "Colosseum feedback",
        botcheck: !!payload._gotcha,
      }
    : { ...payload, _email: { subject } };
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  try {
    const response = await fetcher(endpoint, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        Accept: "application/json",
      },
      body: JSON.stringify(body),
      signal: controller.signal,
      credentials: "omit",
      referrerPolicy: "no-referrer",
    });
    if (!response.ok) throw new Error(sendError(response.status));
    if (web3 && (await response.json()).success !== true)
      throw new Error(sendError(400));
  } catch (error) {
    if (error.name === "AbortError" || error instanceof TypeError)
      throw new Error(
        "The connection did not finish. Keep this draft and try again when you’re online.",
      );
    throw error;
  } finally {
    clearTimeout(timer);
  }
}
