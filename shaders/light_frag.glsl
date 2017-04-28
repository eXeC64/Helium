#version 300 es

precision highp float;

layout (location = 0) out vec3 outColor;

uniform sampler2D sampLambert;
uniform sampler2D sampNormal;
uniform sampler2D sampPBRMaps;
uniform sampler2D sampDepth;

uniform float time;
uniform vec3 viewPos;
uniform vec3 lightPos;
uniform vec3 lightColor;
uniform vec2 screenSize;
uniform mat4 matView;

vec3 gammaCorrect(float gamma, vec3 color)
{
  return pow(color, vec3(1.0/gamma));
}

float normalizeDepth(float value, float min, float max)
{
  float Q = max / (max - min);
  float invFar = 1.0 / max;
  return invFar / (Q - value);
}

vec3 calcWorldPos(vec2 screenPos)
{
 float z = texture(sampDepth, screenPos).x;
 vec4 sPos = vec4(screenPos * 2.0 - 1.0, z * 2.0 - 1.0, 1.0);
 mat4 invMatView = inverse(matView);
 sPos = invMatView * sPos;
 return sPos.xyz / sPos.w;
}

void main()
{
  vec2 screenPos = gl_FragCoord.xy / screenSize;
  vec3 lambert = texture(sampLambert, screenPos).rgb;
  vec3 worldNormal = texture(sampNormal, screenPos).xyz;
  vec3 worldPos = calcWorldPos(screenPos);
  float depth = texture(sampDepth, screenPos).x;

  vec3 lightDir = normalize(lightPos - worldPos);
  float lightPower = clamp(dot(lightDir, worldNormal), 0.0, 1.0);
  float lightDist = distance(lightPos, worldPos);
  float attenuation = 1.0 / (lightDist * lightDist);
  float light = 1.0 * lightPower * attenuation;

  outColor = vec3(0.0);
  if(depth < 1.0)
  {
    outColor = gammaCorrect(2.2, lightColor * light * lambert);
  }
  gl_FragDepth = depth;
}
