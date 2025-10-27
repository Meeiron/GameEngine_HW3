#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <optional>
#include "shader.h"
#include "camera.h"
#include "mesh.h"
#include "model.h"
#include <cmath>

static int SCR_W=1280, SCR_H=720;

struct Cell { enum T {Floor, Wall, Goal} type=Floor; };

struct Grid {
    std::vector<std::string> raw;
    int W=0, H=0;
    glm::ivec2 player{1,1};
    std::vector<glm::ivec2> boxes;
    std::vector<glm::ivec2> goals;

    bool load(const std::string& path){
        std::ifstream f(path);
        if(!f) return false;
        raw.clear();
        std::string line;
        while(std::getline(f, line)){
            if(!line.empty() && line.back()=='\r') line.pop_back();
            raw.push_back(line);
        }
        H = (int)raw.size();
        W = 0;
        for(auto& s: raw) W = std::max(W,(int)s.size());

        boxes.clear(); goals.clear();
        for(int y=0;y<H;++y){
            for(int x=0;x<(int)raw[y].size();++x){
                char c = raw[y][x];
                if(c=='P') player = {x, H-1-y};
                if(c=='B') boxes.push_back({x, H-1-y});
                if(c=='.') goals.push_back({x, H-1-y});
            }
        }
        return true;
    }

    bool isWall(glm::ivec2 p) const {
        int x=p.x, y=p.y;
        int ry = H-1-y;
        if(ry<0||ry>=H||x<0||x>= (int)raw[ry].size()) return true; // outside treated as wall
        return raw[ry][x]=='#';
    }
    bool isGoal(glm::ivec2 p) const {
        for(auto& g: goals) if(g==p) return true;
        return false;
    }
    bool occupiedByBox(glm::ivec2 p, int* idxOut=nullptr) const {
        for(size_t i=0;i<boxes.size();++i) if(boxes[i]==p){ if(idxOut) *idxOut=(int)i; return true; }
        return false;
    }
    bool win() const {
        for(auto& g: goals){
            if(!occupiedByBox(g)) return false;
        }
        return true;
    }
};

// simple unit cube mesh for fallback
Mesh makeCube(){
    Mesh m;
    glm::vec3 p[] = {
        {-0.5f,-0.5f,-0.5f},{0.5f,-0.5f,-0.5f},{0.5f,0.5f,-0.5f},{-0.5f,0.5f,-0.5f},
        {-0.5f,-0.5f, 0.5f},{0.5f,-0.5f, 0.5f},{0.5f,0.5f, 0.5f},{-0.5f,0.5f, 0.5f}
    };
    glm::vec3 n[] = {
        {0,0,-1},{0,0,-1},{0,0,-1},{0,0,-1},
        {0,0,1},{0,0,1},{0,0,1},{0,0,1},
        {-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0},
        {1,0,0},{1,0,0},{1,0,0},{1,0,0},
        {0,-1,0},{0,-1,0},{0,-1,0},{0,-1,0},
        {0,1,0},{0,1,0},{0,1,0},{0,1,0},
    };
    unsigned idx[] = {
        0,1,2, 2,3,0,
        4,5,6, 6,7,4,
        0,3,7, 7,4,0,
        1,5,6, 6,2,1,
        0,4,5, 5,1,0,
        3,2,6, 6,7,3
    };
    // duplicate vertices for normals per face (simple)
    for(int i=0;i<36;i++){
        int k = idx[i];
        Vertex v{}; v.pos = p[k]; v.normal = glm::normalize(v.pos); v.uv={0,0};
        m.vertices.push_back(v);
        m.indices.push_back(i);
    }
    m.upload();
    return m;
}

struct Assets {
    Model player, box, wall, floor;
    bool hasPlayer=false, hasBox=false, hasWall=false, hasFloor=false;
    Mesh cube;
    void load(){
        cube = makeCube();
        hasPlayer = player.load("assets/models/player.gltf") || player.load("assets/models/player.obj");
        hasBox    = box.load("assets/models/box.gltf")    || box.load("assets/models/box.obj");
        hasWall   = wall.load("assets/models/wall.gltf")   || wall.load("assets/models/wall.obj");
        hasFloor  = floor.load("assets/models/floor.gltf")  || floor.load("assets/models/floor.obj");
    }
    void drawModelOrCube(Model& m, bool has, Shader& sh){
        if(has) m.draw();
        else    cube.draw();
    }
};

// Globals
Camera gCam;
Grid gGrid;
Assets gAssets;
glm::vec3 gPlayerWorld{0,0,0};
glm::vec3 gMoveAnimStart{0,0,0};
glm::vec3 gMoveAnimEnd{0,0,0};
float gMoveT=1.0f; // 1 = idle
glm::ivec2 gDir{0,0};

// ---- เพิ่มตรงนี้ (ใต้ตัวแปร globals เดิม) ----
std::vector<std::string> gLevels = {
    "assets/levels/level01.txt",
    "assets/levels/level02.txt",
    "assets/levels/level03.txt"
};
int   gLevelIndex = 0;
bool  gAllCleared = false;
float gWinTimer = 0.0f;

void loadCurrentLevel() {
    if (!gGrid.load(gLevels[gLevelIndex])) {
        std::cerr << "Failed to load level: " << gLevels[gLevelIndex] << "\n";
    }
    gPlayerWorld = glm::vec3(gGrid.player.x, 0, gGrid.player.y);
    gMoveT = 1.0f;
    gDir = { 0,0 };
    gWinTimer = 0.0f;
}

void tryMove(glm::ivec2 d);

void framebuffer_size_callback(GLFWwindow*, int w, int h){ SCR_W=w; SCR_H=h; glViewport(0,0,w,h); gCam.aspect = float(w)/float(h); }

void key_callback(GLFWwindow* win, int key, int sc, int action, int mods) {
    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(win, 1);

        if (key == GLFW_KEY_R) {      
            loadCurrentLevel();
        }

        if (key == GLFW_KEY_ENTER && gAllCleared) {
            gLevelIndex = 0;
            gAllCleared = false;
            loadCurrentLevel();
        }

        if (key == GLFW_KEY_1) gCam.topDown = false;
        if (key == GLFW_KEY_2) gCam.topDown = true;

        if (gMoveT >= 1.0f && !gAllCleared) {
            glm::ivec2 d{ 0,0 };

            if (key == GLFW_KEY_W || key == GLFW_KEY_UP)    d = { 0, -1 };
            if (key == GLFW_KEY_S || key == GLFW_KEY_DOWN)  d = { 0,  1 };
            if (key == GLFW_KEY_A || key == GLFW_KEY_LEFT)  d = { -1, 0 };
            if (key == GLFW_KEY_D || key == GLFW_KEY_RIGHT) d = { 1, 0 };
            if (d != glm::ivec2{ 0,0 }) {
                tryMove(d);
            }
        }
    }
}


bool cellFree(glm::ivec2 p){
    if(gGrid.isWall(p)) return false;
    int idx=-1; if(gGrid.occupiedByBox(p, &idx)) return false;
    return true;
}

void tryMove(glm::ivec2 d){
    glm::ivec2 dest = gGrid.player + d;
    if(gGrid.isWall(dest)) return;
    int idx=-1;
    if(gGrid.occupiedByBox(dest, &idx)){
        glm::ivec2 beyond = dest + d;
        if(cellFree(beyond)){
            gGrid.boxes[idx] = beyond;
            // move player into dest
            gGrid.player = dest;
        } else return; // blocked
    } else {
        gGrid.player = dest;
    }
    gDir = d;
    gMoveAnimStart = gPlayerWorld;
    gMoveAnimEnd   = glm::vec3(gGrid.player.x, 0, gGrid.player.y);
    gMoveT = 0.0f;
}

int main(){
    if(!glfwInit()){ std::cerr<<"glfw init failed\n"; return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* win = glfwCreateWindow(SCR_W,SCR_H,"Sokoban OpenGL",nullptr,nullptr);
    if(!win){ std::cerr<<"window failed\n"; glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSetFramebufferSizeCallback(win, framebuffer_size_callback);
    glfwSetKeyCallback(win, key_callback);
    glfwSwapInterval(1);

    if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)){ std::cerr<<"glad failed\n"; return 1; }

    // Load shaders from disk
    std::ifstream vf("shaders/basic.vert"); std::string vsrc((std::istreambuf_iterator<char>(vf)), {});
    std::ifstream ff("shaders/basic.frag"); std::string fsrc((std::istreambuf_iterator<char>(ff)), {});
    Shader sh(vsrc, fsrc);

    // Load assets
    gAssets.load();

    // Load level
    loadCurrentLevel();
    gPlayerWorld = glm::vec3(gGrid.player.x, 0, gGrid.player.y);

    glEnable(GL_DEPTH_TEST);

    while(!glfwWindowShouldClose(win)){
        glfwPollEvents();

        static double lastT = glfwGetTime();
        double now = glfwGetTime();
        float dt = float(now - lastT);
        lastT = now;

        // animate movement
        if(gMoveT<1.0f){
            gMoveT += 0.15f; if(gMoveT>1.0f) gMoveT=1.0f;
            gPlayerWorld = glm::mix(gMoveAnimStart, gMoveAnimEnd, gMoveT);
        }

        // camera follow
        gCam.follow(gPlayerWorld);

        glClearColor(0.07f,0.08f,0.10f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        sh.use();
        glm::mat4 V = gCam.view();
        glm::mat4 P = gCam.proj();
        sh.setMat4("uView", &V[0][0]);
        sh.setMat4("uProj", &P[0][0]);
        sh.setVec3("uCamPos", gCam.pos.x, gCam.pos.y, gCam.pos.z);
        // dir light
        sh.setVec3("uLightDir", -0.5f, -1.0f, -0.3f);
        sh.setVec3("uLightColor", 1.0f, 1.0f, 1.0f);
        sh.setBool("uHasTexture", false);

        auto drawCubeColored = [&](glm::vec3 pos, glm::vec3 scl, glm::vec3 color){
            glm::mat4 M(1.0f);
            M = glm::translate(M, pos);
            M = glm::scale(M, scl);
            sh.setMat4("uModel",&M[0][0]);
            sh.setColor("uColor", color.x, color.y, color.z);
            gAssets.cube.draw();
        };

        // Draw grid: floors & walls
        for(int y=0;y<gGrid.H;++y){
            for(int x=0;x<gGrid.W;++x){
                glm::ivec2 p{x,y};
                // floor
                glm::vec3 pos = { (float)x, -0.01f, (float)y };
                if(gAssets.hasFloor){
                    glm::mat4 M = glm::translate(glm::mat4(1.0f), pos) * glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 0.02f, 1.0f));
                    sh.setMat4("uModel", &M[0][0]);
                    sh.setColor("uColor", 0.5f,0.5f,0.5f);
                    gAssets.floor.draw();
                } else {
                    drawCubeColored(pos, {1,0.05f,1}, {0.2f,0.25f,0.3f});
                }
                // walls (from raw map)
                int ry = gGrid.H-1-y;
                if(ry>=0 && ry<gGrid.H && x<(int)gGrid.raw[ry].size() && gGrid.raw[ry][x]=='#'){
                    glm::vec3 wpos = { (float)x, 0.5f, (float)y };
                    if(gAssets.hasWall){
                        glm::mat4 M = glm::translate(glm::mat4(1.0f), wpos);
                        M = glm::scale(M, glm::vec3(0.2f));
                        sh.setMat4("uModel", &M[0][0]);
                        sh.setColor("uColor", 0.5f,0.5f,0.55f);
                        gAssets.wall.draw();
                    } else {
                        drawCubeColored(wpos, {1,1,1}, {0.45f,0.45f,0.5f});
                    }
                }
            }
        }

        // goals
        for(auto& g: gGrid.goals){
            drawCubeColored(glm::vec3(g.x, 0.01f, g.y), {0.2f,0.02f,0.2f}, {0.9f,0.85f,0.2f});
        }

        // boxes
        for(auto& b: gGrid.boxes){
            glm::vec3 pos = {(float)b.x, 0.5f, (float)b.y};
            if(gAssets.hasBox){
                glm::mat4 M = glm::translate(glm::mat4(1.0f), pos);
                M = glm::scale(M, glm::vec3(0.15f));
                sh.setMat4("uModel",&M[0][0]);
                sh.setColor("uColor", 0.8f,0.6f,0.3f);
                gAssets.box.draw();
            } else {
                drawCubeColored(pos, {1,1,1}, {0.7f,0.4f,0.2f});
            }
        }

        // player
        {
            glm::vec3 pos = gPlayerWorld + glm::vec3(0,0.5f,0);
            if(gAssets.hasPlayer){
                glm::mat4 M = glm::translate(glm::mat4(1.0f), pos);
                // Face movement direction
                float rotY = std::atan2((float)gDir.y, -(float)gDir.x);
                M = glm::rotate(M, rotY, glm::vec3(0, 1, 0));
                M = glm::scale(M, glm::vec3(0.075f));
                sh.setMat4("uModel",&M[0][0]);
                sh.setColor("uColor", 0.2f,0.7f,0.8f);
                gAssets.player.draw();
            } else {
                drawCubeColored(pos, {1,1,1}, {0.2f,0.7f,0.8f});
            }
        }

        // win text via clear color blink (simple)
        // ----- WIN / LEVEL PROGRESSION -----
        if (gGrid.win()) {
            float t = (float)glfwGetTime();
            glClearColor(0.0f, 0.35f + 0.25f * std::sin(t * 6.0f), 0.0f, 1.0f);

            if (!gAllCleared) {
                if (gLevelIndex < (int)gLevels.size() - 1) {
                    gWinTimer += dt;
                    glfwSetWindowTitle(win, "Level Cleared! Loading next...");
                    if (gWinTimer >= 1.0f) {
                        gLevelIndex++;
                        loadCurrentLevel();
                        glfwSetWindowTitle(win, ("Level " + std::to_string(gLevelIndex + 1)).c_str());
                    }
                }
                else {
                    gAllCleared = true;
                }
            }
        }


        glfwSwapBuffers(win);
    }
    glfwTerminate();
    return 0;
}