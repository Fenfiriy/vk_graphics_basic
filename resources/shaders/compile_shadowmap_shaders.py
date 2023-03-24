import os
import subprocess
import pathlib

if __name__ == '__main__':
    glslang_cmd = "glslangValidator"
    #simple creates shadows, simple_deferred handles Surface Normal G-Buffer and Diffuse Color G-Buffer, simple_shadow blends all together
    shader_list = ["simple.vert", "quad.vert", "quad.frag", "simple_shadow.frag", "simple_deferred.frag"]

    for shader in shader_list:
        subprocess.run([glslang_cmd, "-V", shader, "-o", "{}.spv".format(shader)])

