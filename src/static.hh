#include <string>

static const std::string VERTEX_SHADER = R"#(#version 460
// Original by DTZxPorter
// Tweaked to modern OGL by MrSteyk

layout(location = 0) in vec3 vertPos;
layout(location = 1) in vec3 vertNorm;
layout(location = 3) in vec2 vertUV;

layout(binding = 0, std140) uniform viewInfo {
    mat4 projection;
    mat4 view;
};
layout(location = 1) uniform mat4 model;

out VS_OUTPUT {
    vec3 Normal;
    vec3 FragPos;
    vec2 UVLayer;
} vert;

out gl_PerVertex {
    vec4 gl_Position;
};

void main()
{
  mat4 MVP = projection * view * model;
  
  gl_Position = MVP * vec4(vertPos, 1.0);
  
  // Pass normal, color, and position to frag shader
  vert.Normal = mat3(transpose(inverse(model))) * vertNorm;
  vert.FragPos = vec3(model * vec4(vertPos, 1.0));
  vert.UVLayer = vertUV;
})#";

static const std::string FRAGMENT_SHADER = R"#(#version 460
// Original by DTZxPorter
// Tweaked to modern OGL by MrSteyk

layout(location = 0) out vec3 color;

in VS_OUTPUT {
    // vec3 ColorFrag;
    vec3 Normal;
    vec3 FragPos;
    vec2 UVLayer;
} vert;

layout(binding = 0, std140) uniform viewInfo {
    mat4 projection;
    mat4 view;
};

layout(binding = 0) uniform sampler2D diffuseTexture;
layout(location = 2) uniform int diffuseLoaded;

void main()
{
  float ambientStrength = 0.1;
  vec3 ambient = ambientStrength * vec3(1, 1, 1);   // Amb color

  vec3 norm = normalize(vert.Normal);
  vec3 lightDir = normalize(inverse(view)[3].xyz - vert.FragPos);
  float diff = max(dot(norm, lightDir), 0.001);
  vec3 diffuse = diff * vec3(1, 1, 1);  // Light color

  if (diffuseLoaded == 1) {
    color = (ambient + diffuse) * texture(diffuseTexture, vert.UVLayer).rgb;
  } else if(diffuseLoaded == 2) {
    color = (ambient + diffuse) * vert.Normal;
  } else {
    color = (ambient + diffuse) * vec3(0.603, 0.603, 0.603);
  }
})#";