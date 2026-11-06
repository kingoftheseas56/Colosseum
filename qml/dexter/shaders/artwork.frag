VARYING vec2 artworkUV;
void MAIN()
{
    vec4 sampleColor = texture(imageTexture, artworkUV);
    FRAGCOLOR = vec4(sampleColor.rgb, sampleColor.a * imageOpacity);
}
