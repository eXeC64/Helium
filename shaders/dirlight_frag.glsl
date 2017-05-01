#version 300 es

precision highp float;

layout (location = 0) out vec3 outColor;

uniform sampler2D sampLambert;
uniform sampler2D sampNormal;
uniform sampler2D sampPBRMaps;
uniform sampler2D sampDepth;

uniform vec3 lightDir;
uniform vec3 lightColor;
uniform vec2 screenSize;

vec3 gammaCorrect(float gamma, vec3 color)
{
  return pow(color, vec3(1.0/gamma));
}

void main()
{
  vec2 screenPos = gl_FragCoord.xy / screenSize;
  vec3 lambert = texture(sampLambert, screenPos).rgb;
  vec3 worldNormal = texture(sampNormal, screenPos).xyz;
  float depth = texture(sampDepth, screenPos).x;

  float cosTheta = max(dot(normalize(lightDir), worldNormal), 0.0);
  vec3 radiance = lightColor * cosTheta;

  outColor = vec3(0.0);
  if(depth < 1.0)
  {
    outColor = gammaCorrect(2.2, radiance * lambert);
  }
  gl_FragDepth = depth;
}