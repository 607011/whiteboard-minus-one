// Copyright (c) 2015 Oliver Lau <ola@ct.de>
// All rights reserved.

#version 150 core
#extension GL_EXT_gpu_shader4 : enable

in vec4 aVertex;
in vec4 aTexCoord;

uniform mat4 uMatrix;

smooth out vec2 vTexCoord;

void main(void)
{
  vTexCoord = aTexCoord.st;
  gl_Position = uMatrix * aVertex;
}
