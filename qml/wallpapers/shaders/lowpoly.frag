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
// monochrome silver ramp (2026-07-25, Hemanth): near-black -> dark grey -> slate ->
// cool silver highlight. Dark-dominant with a faint cool tint (b >= g >= r) so the
// bright facets read as steel/silver, never a warm or bright-white wash — keeps the
// glass UI + gold accents legible. Same interpolation as before, so the faceted glass
// depth is untouched; only the palette changed (replacing the old purple/magenta).
vec3 grad(float t){
    t = clamp(t,0.0,1.0);
    vec3 c0 = vec3(0.040,0.045,0.060);
    vec3 c1 = vec3(0.130,0.140,0.170);
    vec3 c2 = vec3(0.340,0.360,0.400);
    vec3 c3 = vec3(0.660,0.690,0.740);
    if(t < 0.42) return mix(c0,c1, t/0.42);
    else if(t < 0.78) return mix(c1,c2, (t-0.42)/0.36);
    else return mix(c2,c3, (t-0.78)/0.22);
}
void main(){
    vec2 uv = qt_TexCoord0;
    float aspect = iResolution.x/iResolution.y;
    vec2 p = uv; p.x *= aspect;
    float N = 13.0;
    vec2 g = p * N;
    vec2 cell = floor(g);
    vec2 f = fract(g);
    float upper = step(1.0, f.x + f.y);
    vec2 triCen = (upper > 0.5) ? vec2(0.6667,0.6667) : vec2(0.3333,0.3333);
    vec2 tc = cell + triCen;
    float t = iTime * 0.04;
    float field = fbm(tc*0.19 + vec2(t, t*0.6)) + 0.12*fbm(tc*0.5 - vec2(t*0.4,0.0));
    field = smoothstep(0.15, 0.95, field);
    vec3 col = grad(field);
    // subtle faceted edge shading
    float edge = min(min(f.x, f.y), abs(1.0 - f.x - f.y));
    col *= 0.86 + 0.14*smoothstep(0.0, 0.09, edge);
    // gentle vignette
    vec2 q = uv - 0.5; col *= 1.0 - 0.25*dot(q,q);
    fragColor = vec4(col, 1.0) * qt_Opacity;
}
