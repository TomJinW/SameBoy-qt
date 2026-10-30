#version 440
layout(location=0) out vec2 uv;
layout(std140,binding=0) uniform Params {
    mat4 mvp;
    vec2 output_resolution;
    int frame_blending_mode;
    int reserved;
};
void main()
{
    vec2 pos[4]=vec2[](vec2(-1,1),vec2(-1,-1),vec2(1,1),vec2(1,-1));
    vec2 coords[4]=vec2[](vec2(0,0),vec2(0,1),vec2(1,0),vec2(1,1));
    uv=coords[gl_VertexIndex];
    gl_Position=mvp*vec4(pos[gl_VertexIndex],0,1);
}
