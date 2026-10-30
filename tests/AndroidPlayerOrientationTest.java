import org.colosseum.platform.PlayerOrientationRequest;

public final class AndroidPlayerOrientationTest {
    private static void expect(int actual, int expected) {
        if (actual != expected) throw new AssertionError(actual + " != " + expected);
    }
    public static void main(String[] args) {
        PlayerOrientationRequest policy = new PlayerOrientationRequest();
        expect(policy.update(false, -1), -1);
        expect(policy.update(true, -1), 6);
        expect(policy.update(true, 6), 6);
        // Source switching/resume must never save landscape as the old request.
        expect(policy.update(true, 1), 6);
        expect(policy.update(false, 6), -1);
        expect(policy.update(false, 1), 1);
        expect(policy.update(true, 1), 6);
        if (!policy.active()) throw new AssertionError("Missing orientation ownership");
        expect(policy.update(false, 6), 1);
        if (policy.active()) throw new AssertionError("Orientation ownership retained");
        System.out.println("PASS: sensor landscape, repeated enter/resume, previous request, repeated exit");
    }
}
