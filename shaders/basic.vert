#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTex;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;

out vec3 vNormal;
out vec3 vWorldPos;
out vec2 vTex;

void main(){
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    mat3 nmat = mat3(transpose(inverse(uModel)));
    vNormal = normalize(nmat * aNormal);
    vTex = aTex;
    gl_Position = uProj * uView * world; 
}
