VARYING vec2 artworkUV;
void MAIN()
{
    artworkUV = vec2(UV0.x, flipArtworkY ? 1.0 - UV0.y : UV0.y);
    POSITION = MODELVIEWPROJECTION_MATRIX * vec4(VERTEX, 1.0);
}
