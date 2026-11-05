import org.colosseum.feedback.FeedbackNavigationPolicy;

public final class FeedbackNavigationPolicyTest {
    public static void main(String[] args) {
        check(FeedbackNavigationPolicy.document("https://getcolosseum.world/feedback/?version=1.2.1&platform=android"));
        for (String url : new String[]{"http://getcolosseum.world/feedback/", "https://getcolosseum.world:444/feedback/", "https://getcolosseum.world@evil.example/feedback/", "https://getcolosseum.world/feedback/../account", "https://getcolosseum.world/feedback/%2e%2e/account", "file:///feedback/", "javascript:alert(1)", "https://getcolosseum.world/account"}) check(!FeedbackNavigationPolicy.document(url));
        check(FeedbackNavigationPolicy.document("https://getcolosseum.world/feedback/?version=1.2.1&platform=android&mode=universe"));
        String wikipedia = "https://en.wikipedia.org/w/api.php?action=query&origin=*";
        check(FeedbackNavigationPolicy.resource(wikipedia));
        check(!FeedbackNavigationPolicy.document(wikipedia));
        check(FeedbackNavigationPolicy.resource("https://getcolosseum.world/feedback/style.css"));
        check(FeedbackNavigationPolicy.resource("https://submit-form.com/form-id"));
        check(FeedbackNavigationPolicy.resource("https://api.web3forms.com/submit"));
        check(FeedbackNavigationPolicy.resource("https://challenges.cloudflare.com/turnstile/v0/api.js"));
        check(FeedbackNavigationPolicy.resource("https://new-form-provider.example/submit"));
        check(!FeedbackNavigationPolicy.resource("http://new-form-provider.example/submit"));
        check(!FeedbackNavigationPolicy.resource("https://user:secret@new-form-provider.example/submit"));
        check(!FeedbackNavigationPolicy.resource("file:///sdcard/private"));
        System.out.println("PASS feedback Android navigation policy");
    }
    private static void check(boolean value) { if (!value) throw new AssertionError(); }
}
