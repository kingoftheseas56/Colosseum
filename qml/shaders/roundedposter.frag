#version 440
// RoundedPosterImage's single render pass: the neutral placeholder gradient, the decoded art
// cropped like Image.PreserveAspectCrop (centred) and faded in by artMix, and the rounded-rect
// mask with a 1 px antialiased edge. Replaces a MultiEffect mask over two layer FBOs.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 itemSize;
    float radius;
    float artMix;
    vec2 uvScale;
    vec2 uvOffset;
};
layout(binding = 1) uniform sampler2D source;

float roundedCoverage(vec2 p) {
    vec2 halfSize = itemSize * 0.5;
    float r = min(radius, min(halfSize.x, halfSize.y));
    vec2 q = abs(p - halfSize) - (halfSize - vec2(r));
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
    return clamp(0.5 - d, 0.0, 1.0);
}

// #191b21 at the top, #101218 in the middle, #17171b at the bottom (the old placeholder Rectangle).
vec3 placeholder(float t) {
    vec3 top = vec3(25.0, 27.0, 33.0) / 255.0;
    vec3 mid = vec3(16.0, 18.0, 24.0) / 255.0;
    vec3 bot = vec3(23.0, 23.0, 27.0) / 255.0;
    return t < 0.5 ? mix(top, mid, t * 2.0) : mix(mid, bot, (t - 0.5) * 2.0);
}

void main() {
    float coverage = roundedCoverage(qt_TexCoord0 * itemSize);
    vec4 art = texture(source, uvOffset + qt_TexCoord0 * uvScale) * artMix;   // premultiplied
    vec3 rgb = placeholder(qt_TexCoord0.y) * (1.0 - art.a) + art.rgb;
    fragColor = vec4(rgb, 1.0) * (coverage * qt_Opacity);
}
