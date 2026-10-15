#version 440
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float iTime;
    vec2 iResolution;
};
float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1,311.7)))*43758.5453); }
float vnoise(vec2 p){
    vec2 i = floor(p), f = fract(p);
    vec2 u = f*f*(3.0-2.0*f);
    return mix(mix(hash(i+vec2(0,0)), hash(i+vec2(1,0)), u.x),
               mix(hash(i+vec2(0,1)), hash(i+vec2(1,1)), u.x), u.y);
}
float fbm(vec2 p){ float v=0.0,a=0.5; for(int i=0;i<4;i++){ v+=a*vnoise(p); p*=2.0; a*=0.5; } return v; }
// Noir Flow: domain-warped value noise (Inigo-Quilez-style two-level warp), animated so
// silver light currents drift through deep black. Monochrome, dark-dominant.
void main(){
    vec2 uv = qt_TexCoord0;
    float aspect = iResolution.x / iResolution.y;
    vec2 p = vec2(uv.x * aspect, uv.y) * 2.0;   // square-ish cells, soft/large blobs
    float t = iTime * 0.06;                      // slow flow
    vec2 q = vec2(fbm(p), fbm(p + vec2(5.2, 1.3)));
    vec2 r = vec2(fbm(p + 4.0*q + vec2(1.7, 9.2) + vec2(t, 0.0)),
                  fbm(p + 4.0*q + vec2(8.3, 2.8) + vec2(0.0, t*0.7)));
    float v = fbm(p + 4.0*r);
    // dark-dominant: silver only in the ridges (balanced level ~0.82)
    float s = clamp((v - 0.32) / 0.5, 0.0, 1.0);
    s = pow(s, 1.5) * 0.82;
    float val = 0.024 + s * 0.88;                // 0 .. ~0.90 (silver, never blown white)
    vec3 col = val * vec3(0.97, 1.00, 1.08);     // faint cool tint (b >= g >= r) → steel/silver
    vec2 d = uv - 0.5; col *= 1.0 - 0.28*dot(d, d);   // gentle vignette
    fragColor = vec4(col, 1.0) * qt_Opacity;
}
