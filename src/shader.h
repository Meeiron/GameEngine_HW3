#pragma once
#include <string>
#include <stdexcept>
#include <glad/glad.h>

class Shader {
public:
    GLuint id{};
    Shader(const std::string& vsSource, const std::string& fsSource){
        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        const char* vsrc = vsSource.c_str();
        glShaderSource(vs,1,&vsrc,nullptr);
        glCompileShader(vs);
        check(vs, true);

        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        const char* fsrc = fsSource.c_str();
        glShaderSource(fs,1,&fsrc,nullptr);
        glCompileShader(fs);
        check(fs, true);

        id = glCreateProgram();
        glAttachShader(id, vs);
        glAttachShader(id, fs);
        glLinkProgram(id);
        check(id, false);

        glDeleteShader(vs);
        glDeleteShader(fs);
    }
    void use() const { glUseProgram(id); }
    void setMat4(const char* name, const float* ptr) const {
        glUniformMatrix4fv(glGetUniformLocation(id, name), 1, GL_FALSE, ptr);
    }
    void setVec3(const char* name, float x, float y, float z) const {
        glUniform3f(glGetUniformLocation(id, name), x,y,z);
    }
    void setInt(const char* name, int v) const {
        glUniform1i(glGetUniformLocation(id, name), v);
    }
    void setBool(const char* name, bool v) const {
        glUniform1i(glGetUniformLocation(id, name), v ? 1 : 0);
    }
    void setColor(const char* name, float r, float g, float b) const {
        glUniform3f(glGetUniformLocation(id, name), r,g,b);
    }
private:
    static void check(GLuint obj, bool shader){
        GLint ok=0;
        if(shader){
            glGetShaderiv(obj, GL_COMPILE_STATUS, &ok);
            if(!ok){
                char log[2048]; GLsizei len=0;
                glGetShaderInfoLog(obj, 2048, &len, log);
                std::cerr << "Shader compile error log:\n" << log << std::endl;

                throw std::runtime_error(std::string("Shader compile error: ") + log);
            }
        } else {
            glGetProgramiv(obj, GL_LINK_STATUS, &ok);
            if(!ok){
                char log[2048]; GLsizei len=0;
                glGetProgramInfoLog(obj, 2048, &len, log);
                // ก่อน throw ในส่วนโปรแกรมลิงก์:
                std::cerr << "Program link error log:\n" << log << std::endl;

                throw std::runtime_error(std::string("Program link error: ") + log);
            }
        }
    }
};