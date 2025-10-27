#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct Camera {
    glm::vec3 pos{0,3,6};
    glm::vec3 target{0,0,0};
    glm::vec3 up{0,1,0};
    float fov = 60.0f;
    float aspect = 16.0f/9.0f;
    float nearP = 0.1f;
    float farP = 100.0f;
    bool topDown = false;

    glm::mat4 view() const {
        return glm::lookAt(pos, target, up);
    }
    glm::mat4 proj() const {
        return glm::perspective(glm::radians(fov), aspect, nearP, farP);
    }
    void follow(const glm::vec3& p){
        if(topDown){
            pos = p + glm::vec3(0, 10.0f, 0.001f);
            target = p;
            up = glm::vec3(0,0,-1);
        } else {
            pos = p + glm::vec3(0.0f, 7.0f, 5.0f);
            target = p + glm::vec3(0,0,0);
            up = glm::vec3(0,1,0);
        }
    }
};