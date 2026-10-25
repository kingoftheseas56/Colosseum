package org.colosseum.platform;

import android.media.MediaCodecInfo.CodecProfileLevel;

/** Host checks of production normalization; OS enumeration is qualified on Android. */
public final class DeviceVideoCapabilitiesTest {
    private static int assertions;

    private static void equal(Object actual, Object expected, String label) {
        assertions++;
        if (!expected.equals(actual))
            throw new AssertionError(label + ": " + actual + " != " + expected);
    }

    public static void main(String[] args) {
        equal(DeviceVideoCapabilities.profileName("video/hevc", CodecProfileLevel.HEVCProfileMain), "main", "HEVC Main");
        equal(DeviceVideoCapabilities.profileName("video/hevc", CodecProfileLevel.HEVCProfileMain10), "main10", "HEVC Main10");
        equal(DeviceVideoCapabilities.profileName("video/hevc", CodecProfileLevel.HEVCProfileMain10HDR10), "main10", "HDR10 is Main10");
        equal(DeviceVideoCapabilities.profileName("video/hevc", CodecProfileLevel.HEVCProfileMain10HDR10Plus), "main10", "HDR10+ is Main10");
        equal(DeviceVideoCapabilities.profileName("video/av01", CodecProfileLevel.AV1ProfileMain8), "main", "AV1 Main8");
        equal(DeviceVideoCapabilities.profileName("video/av01", CodecProfileLevel.AV1ProfileMain10HDR10), "main10", "AV1 Main10 HDR");
        equal(DeviceVideoCapabilities.profileName("video/avc", CodecProfileLevel.AVCProfileBaseline), "baseline", "AVC baseline");
        equal(DeviceVideoCapabilities.profileName("video/avc", CodecProfileLevel.AVCProfileConstrainedBaseline), "baseline", "AVC constrained baseline");
        equal(DeviceVideoCapabilities.profileName("video/avc", CodecProfileLevel.AVCProfileMain), "main", "AVC main");
        equal(DeviceVideoCapabilities.profileName("video/avc", CodecProfileLevel.AVCProfileConstrainedHigh), "high", "AVC constrained high");
        equal(DeviceVideoCapabilities.profileName("video/avc", CodecProfileLevel.AVCProfileHigh10), "high10", "AVC high10");
        equal(DeviceVideoCapabilities.profileName("video/avc", CodecProfileLevel.AVCProfileHigh422), "high422", "AVC 422");
        equal(DeviceVideoCapabilities.profileName("video/avc", CodecProfileLevel.AVCProfileHigh444), "high444", "AVC 444");
        equal(DeviceVideoCapabilities.profileName("video/hevc", Integer.MAX_VALUE), "", "future HEVC profile stays unknown");
        equal(DeviceVideoCapabilities.profileName("video/unknown", 1), "", "unknown MIME stays unknown");
        equal(DeviceVideoCapabilities.avcLevel(CodecProfileLevel.AVCLevel41), 4.1, "AVC 4.1");
        equal(DeviceVideoCapabilities.avcLevel(CodecProfileLevel.AVCLevel52), 5.2, "AVC 5.2");
        equal(DeviceVideoCapabilities.avcLevel(CodecProfileLevel.AVCLevel62), 6.2, "AVC 6.2");
        equal(DeviceVideoCapabilities.avcLevel(CodecProfileLevel.AVCLevel1b), 0.0, "1b is not an invented decimal level");
        equal(DeviceVideoCapabilities.avcLevel(Integer.MAX_VALUE), 0.0, "future AVC level stays unknown");
        System.out.println("DEVICE_VIDEO_NORMALIZATION_OK assertions=" + assertions);
    }
}
