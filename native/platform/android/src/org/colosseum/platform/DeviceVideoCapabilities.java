package org.colosseum.platform;

import android.media.MediaCodecInfo;
import android.media.MediaCodecInfo.CodecCapabilities;
import android.media.MediaCodecInfo.CodecProfileLevel;
import android.media.MediaCodecList;
import android.os.Build;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.Locale;

/** Read-only device enumeration, cached for this process. Software decoders count. */
public final class DeviceVideoCapabilities {
    private static String cached;

    private DeviceVideoCapabilities() {}

    public static synchronized String snapshot() {
        if (cached != null)
            return cached;
        JSONArray decoders = new JSONArray();
        boolean complete = true;
        try {
            MediaCodecInfo[] infos = new MediaCodecList(MediaCodecList.REGULAR_CODECS).getCodecInfos();
            for (MediaCodecInfo info : infos) {
                if (info.isEncoder())
                    continue;
                for (String type : info.getSupportedTypes()) {
                    String mime = type.toLowerCase(Locale.ROOT);
                    if (!isRelevantMime(mime))
                        continue;
                    try {
                        CodecCapabilities caps = info.getCapabilitiesForType(type);
                        JSONArray profiles = new JSONArray();
                        boolean profilesComplete = caps.profileLevels != null && caps.profileLevels.length > 0;
                        if (caps.profileLevels != null) {
                            for (CodecProfileLevel profile : caps.profileLevels) {
                                String name = profileName(mime, profile.profile);
                                if (name.isEmpty()) {
                                    profilesComplete = false;
                                    continue;
                                }
                                profiles.put(new JSONObject().put("profile", name)
                                    .put("level", mime.equals("video/avc") ? avcLevel(profile.level) : 0.0));
                            }
                        }
                        JSONObject decoder = new JSONObject().put("name", info.getName())
                            .put("mime", mime).put("profiles", profiles)
                            .put("profilesComplete", profilesComplete);
                        if (Build.VERSION.SDK_INT >= 29)
                            decoder.put("hardware", info.isHardwareAccelerated());
                        decoders.put(decoder);
                    } catch (RuntimeException | LinkageError error) {
                        // A failed codec query cannot justify a negative result for this device.
                        complete = false;
                    }
                }
            }
            cached = new JSONObject().put("available", true).put("complete", complete)
                .put("decoders", decoders).toString();
        } catch (Exception | LinkageError error) {
            cached = "{\"available\":false,\"complete\":false,\"decoders\":[]}";
        }
        return cached;
    }

    private static boolean isRelevantMime(String mime) {
        return mime.equals("video/hevc") || mime.equals("video/avc")
            || mime.equals("video/av01") || mime.equals("video/dolby-vision");
    }

    static String profileName(String mime, int profile) {
        if (mime.equals("video/hevc")) {
            if (profile == CodecProfileLevel.HEVCProfileMain) return "main";
            if (profile == CodecProfileLevel.HEVCProfileMain10
                    || profile == CodecProfileLevel.HEVCProfileMain10HDR10
                    || profile == CodecProfileLevel.HEVCProfileMain10HDR10Plus) return "main10";
            if (profile == CodecProfileLevel.HEVCProfileMainStill) return "main-still";
        } else if (mime.equals("video/av01")) {
            if (profile == CodecProfileLevel.AV1ProfileMain8) return "main";
            if (profile == CodecProfileLevel.AV1ProfileMain10
                    || profile == CodecProfileLevel.AV1ProfileMain10HDR10
                    || profile == CodecProfileLevel.AV1ProfileMain10HDR10Plus) return "main10";
        } else if (mime.equals("video/avc")) {
            switch (profile) {
            case CodecProfileLevel.AVCProfileBaseline:
            case CodecProfileLevel.AVCProfileConstrainedBaseline: return "baseline";
            case CodecProfileLevel.AVCProfileMain: return "main";
            case CodecProfileLevel.AVCProfileExtended: return "extended";
            case CodecProfileLevel.AVCProfileHigh:
            case CodecProfileLevel.AVCProfileConstrainedHigh: return "high";
            case CodecProfileLevel.AVCProfileHigh10: return "high10";
            case CodecProfileLevel.AVCProfileHigh422: return "high422";
            case CodecProfileLevel.AVCProfileHigh444: return "high444";
            default: return "";
            }
        } else if (mime.equals("video/dolby-vision")) {
            // D1 only classifies the DV family; retaining raw profiles avoids inventing HEVC support.
            return "dv-" + profile;
        }
        return "";
    }

    static double avcLevel(int level) {
        switch (level) {
        case CodecProfileLevel.AVCLevel1: return 1.0;
        case CodecProfileLevel.AVCLevel11: return 1.1;
        case CodecProfileLevel.AVCLevel12: return 1.2;
        case CodecProfileLevel.AVCLevel13: return 1.3;
        case CodecProfileLevel.AVCLevel2: return 2.0;
        case CodecProfileLevel.AVCLevel21: return 2.1;
        case CodecProfileLevel.AVCLevel22: return 2.2;
        case CodecProfileLevel.AVCLevel3: return 3.0;
        case CodecProfileLevel.AVCLevel31: return 3.1;
        case CodecProfileLevel.AVCLevel32: return 3.2;
        case CodecProfileLevel.AVCLevel4: return 4.0;
        case CodecProfileLevel.AVCLevel41: return 4.1;
        case CodecProfileLevel.AVCLevel42: return 4.2;
        case CodecProfileLevel.AVCLevel5: return 5.0;
        case CodecProfileLevel.AVCLevel51: return 5.1;
        case CodecProfileLevel.AVCLevel52: return 5.2;
        case CodecProfileLevel.AVCLevel6: return 6.0;
        case CodecProfileLevel.AVCLevel61: return 6.1;
        case CodecProfileLevel.AVCLevel62: return 6.2;
        default: return 0.0;
        }
    }
}
