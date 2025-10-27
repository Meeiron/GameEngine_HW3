#version 330 core
out vec4 FragColor;

in vec3 vNormal;
in vec3 vWorldPos;
in vec2 vTex;

uniform vec3 uCamPos;
uniform vec3 uColor;
uniform sampler2D uDiffuse;
uniform bool uHasTexture;

uniform vec3 uLightDir;
uniform vec3 uLightColor;

void main(){
    vec3 N = normalize(vNormal);
    vec3 L = normalize(-uLightDir);
    float NdotL = max(dot(N, L), 0.0);
    vec3 base = uHasTexture ? texture(uDiffuse, vTex).rgb : uColor;
    vec3 lit = base * (0.15 + NdotL) * uLightColor;
    FragColor = vec4(lit, 1.0);
}
