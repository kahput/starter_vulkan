float calculate_shadow(vec4 lightspace, float bias) {
    vec2 poissonDisk[4] = vec2[](
      vec2( -0.94201624, -0.39906216 ),
      vec2( 0.94558609, -0.76890725 ),
      vec2( -0.094184101, -0.92938870 ),
      vec2( 0.34495938, 0.29387760 )
    );

    vec3 ndc = lightspace.xyz / lightspace.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;

    float visibility  = 1.0f;
    for (int i=0;i<4;i++){
        if (texture(u_shadow, vec3(uv + poissonDisk[i]/700.0, ndc.z)).r < ndc.z-bias) {
            visibility-=0.2;
        }
    }

    if (ndc.z > 1.0)
        visibility = 1.0;

    return visibility;
}
