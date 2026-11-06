VARYING vec2 artworkUV;
void MAIN()
{
    artworkUV = UV0;
    POSITION = MODELVIEWPROJECTION_MATRIX * vec4(VERTEX, 1.0);
}
