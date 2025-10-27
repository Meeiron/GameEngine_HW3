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
#include "collision.h"
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

struct Entity {
    AABB box;
    glm::vec3 world; 
    glm::vec3 color;
    float scale = 1.0f;
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

std::vector<AABB> gStaticWalls;  
Entity gPlayerEnt;
std::vector<Entity> gBoxEnts;

static inline bool boxOnGoal(const Entity& e, const glm::ivec2& g) {
    // เผื่อคลาดจุดศูนย์กลางเล็กน้อย
    const float tol = 0.3f; // กล่องอยู่ใน cell +-0.3
    return std::abs(e.box.center.x - g.x) <= tol &&
        std::abs(e.box.center.y - g.y) <= tol;
}

bool winAABB() {
    for (const auto& g : gGrid.goals) {
        bool covered = false;
        for (const auto& e : gBoxEnts) {
            if (boxOnGoal(e, g)) { covered = true; break; }
        }
        if (!covered) return false;
    }
    return true;
}

static inline AABB makeTileAABB(int gx, int gy) {
    return { glm::vec2(gx, gy), glm::vec2(0.5f, 0.5f) };
}

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

    // 1) สร้าง AABB ของกำแพงจากแผนที่ (#)
    gStaticWalls.clear();
    for (int y = 0; y < gGrid.H; ++y) {
        for (int x = 0; x < gGrid.W; ++x) {
            int ry = gGrid.H - 1 - y;
            if (ry >= 0 && ry < gGrid.H && x < (int)gGrid.raw[ry].size() && gGrid.raw[ry][x] == '#') {
                gStaticWalls.push_back(makeTileAABB(x, y)); // half = {0.5,0.5}
            }
        }
    }

    // ขนาดคอลลิเดอร์ (half extents) — ปรับเล็กลงให้เดิน/เลาะมุมง่ายขึ้น
    constexpr float PLAYER_HALF = 0.38f; // เดิม 0.45f
    constexpr float BOX_HALF = 0.40f; // เดิม 0.45f (ยังเกือบเต็มช่อง กันลอดซอก)

    // 2) ตั้งคอลลิเดอร์ผู้เล่น
    gPlayerEnt.box.center = glm::vec2(gGrid.player.x, gGrid.player.y);
    gPlayerEnt.box.half = glm::vec2(PLAYER_HALF, PLAYER_HALF);
    gPlayerEnt.world = glm::vec3(gGrid.player.x, 0, gGrid.player.y);
    gPlayerEnt.scale = 1.0f;

    // 3) ตั้งคอลลิเดอร์กล่องตามเลเวล
    gBoxEnts.clear();
    gBoxEnts.reserve(gGrid.boxes.size());
    for (auto& b : gGrid.boxes) {
        Entity e;
        e.box.center = glm::vec2(b.x, b.y);
        e.box.half = glm::vec2(BOX_HALF, BOX_HALF);
        e.world = glm::vec3(b.x, 0, b.y);
        e.scale = 1.0f;
        gBoxEnts.push_back(e);
    }

    // 4) depenetration สั้น ๆ กันซ้อนกำแพงตอนเริ่ม (ผู้เล่น/กล่อง)
    auto depen = [&](AABB& a) {
        for (int it = 0; it < 4; ++it) {
            bool any = false;
            for (const auto& w : gStaticWalls) {
                glm::vec2 aMin = a.center - a.half, aMax = a.center + a.half;
                glm::vec2 bMin = w.center - w.half, bMax = w.center + w.half;
                bool overlap = !(aMax.x < bMin.x || aMin.x > bMax.x || aMax.y < bMin.y || aMin.y > bMax.y);
                if (overlap) {
                    float ox = std::min(aMax.x - bMin.x, bMax.x - aMin.x);
                    float oz = std::min(aMax.y - bMin.y, bMax.y - aMin.y);
                    if (ox < oz) a.center.x += (a.center.x < w.center.x ? -ox : +ox) * 1.001f;
                    else         a.center.y += (a.center.y < w.center.y ? -oz : +oz) * 1.001f;
                    any = true;
                }
            }
            if (!any) break;
        }
        };
    depen(gPlayerEnt.box);
    for (auto& e : gBoxEnts) depen(e.box);

    // 5) รีเซ็ตสถานะการเคลื่อน
    gPlayerWorld = glm::vec3(gPlayerEnt.box.center.x, 0, gPlayerEnt.box.center.y);
    gMoveT = 1.0f;
    gDir = { 0,0 };
    gWinTimer = 0.0f;
}


// พยายามขยับ player ด้วย deltaGrid (เช่น {+1,0} หรือ {0,-1}) แบบต่อเนื่อง
void physicsTryMovePlayer(glm::ivec2 dir) {
    const float speed = 1.0f;                 // 1 tile ต่อครั้ง
    glm::vec2 delta = glm::vec2(dir.x, dir.y) * speed;

    // 1) player vs walls (slide)
    glm::vec2 hitN{ 0,0 };
    moveAndCollide(gPlayerEnt.box, delta, gStaticWalls, &hitN);

    // 2) ตรวจชนกับ boxes แบบต่อเนื่อง:
    //    ลอง sweep กับแต่ละกล่อง ถ้าชน เราจะ "ผลัก" กล่องต่อไปในทิศเดียวกัน
    //    โดยกล่องจะต้อง sweep ผ่านกำแพง/กล่องอื่นได้ (ถ้าไม่ได้ → บล็อก player)
    for (size_t i = 0; i < gBoxEnts.size(); ++i) {
        SweepHit h = sweepAABB(gPlayerEnt.box, glm::vec2(0), gBoxEnts[i].box);
        // h.toi == 0 แปลว่าทับซ้อนอยู่พอดี ให้ตรวจเฉพาะเมื่อผู้เล่นพยายามเข้าไปทับ
    }

    // ทางที่ง่ายกว่า: ลองเคลื่อน player เล็ก ๆ ในทิศ dir ทีละแกน พร้อมทดสอบชนกับ box ด้วย sweep
    // เพื่อให้ push ทำงานชัวร์แบบ discrete step (1 tile) แต่ยังคงเป็น AABB จริง
}

// Helper: ลองผลักกล่อง j ด้วย delta; คืน true ถ้าผ่าน (อัปเดตตำแหน่ง), false ถ้าติด
bool tryMoveBox(size_t j, glm::vec2 delta, glm::vec2* movedOut) {
    glm::vec2 old = gBoxEnts[j].box.center;

    glm::vec2 n;
    moveAndCollide(gBoxEnts[j].box, delta, gStaticWalls, &n);

    // ห้ามชนกล่องอื่น -> ถ้าชน revert
    for (size_t k = 0; k < gBoxEnts.size(); ++k) {
        if (k == j) continue;
        auto& A = gBoxEnts[j].box;
        auto& B = gBoxEnts[k].box;
        glm::vec2 aMin = A.center - A.half, aMax = A.center + A.half;
        glm::vec2 bMin = B.center - B.half, bMax = B.center + B.half;
        bool overlap = !(aMax.x<bMin.x || aMin.x>bMax.x || aMax.y<bMin.y || aMin.y>bMax.y);
        if (overlap) {
            gBoxEnts[j].box.center = old;     // << รีเวิร์ต
            if (movedOut) *movedOut = glm::vec2(0);
            return false;
        }
    }

    if (movedOut) *movedOut = gBoxEnts[j].box.center - old; // ระยะที่ขยับจริง (อาจถูก clip)
    return true;
}

// เวอร์ชันง่าย (แนะนำเริ่มจากอันนี้): ขยับแบบ "หนึ่งช่อง" แต่ทดสอบชนแบบ AABB จริง
void tryMoveDiscreteAABB(glm::ivec2 d) {
    glm::vec2 targetDelta = glm::vec2(d.x, d.y);  // 1 ช่อง

    // ถ้าช่องหน้ามีกล่อง → ทดลองผลักกล่องก่อน
    // สแกนหา box ที่อยู่หน้า player (AABB overlap หลัง apply delta เล็ก ๆ)
    int hitBox = -1;
    AABB probe = gPlayerEnt.box; probe.center += targetDelta;
    for (size_t i = 0; i < gBoxEnts.size(); ++i) {
        // ทดสอบ overlap AABB simple
        glm::vec2 aMin = probe.center - probe.half, aMax = probe.center + probe.half;
        glm::vec2 bMin = gBoxEnts[i].box.center - gBoxEnts[i].box.half, bMax = gBoxEnts[i].box.center + gBoxEnts[i].box.half;
        bool overlap = !(aMax.x < bMin.x || aMin.x > bMax.x || aMax.y < bMin.y || aMin.y > bMax.y);
        if (overlap) { hitBox = (int)i; break; }
    }

    if (hitBox >= 0) {
        // ลองขยับกล่องก่อน
        glm::vec2 moved;
        if (tryMoveBox((size_t)hitBox, targetDelta, &moved)) {
            // กล่องไปได้ → ค่อยขยับผู้เล่น
            glm::vec2 n;
            moveAndCollide(gPlayerEnt.box, moved, gStaticWalls, &n);
        }
        else {
            // กล่องไปไม่ได้ → ผู้เล่นไม่ไป
        }
    }
    else {
        // ขยับผู้เล่นชนกำแพงพร้อม slide
        glm::vec2 n; moveAndCollide(gPlayerEnt.box, targetDelta, gStaticWalls, &n);
    }
}


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

        //if (gMoveT >= 1.0f && !gAllCleared) {
        //    glm::ivec2 d{ 0,0 };

        //    if (key == GLFW_KEY_W || key == GLFW_KEY_UP)    d = { 0, -1 };
        //    if (key == GLFW_KEY_S || key == GLFW_KEY_DOWN)  d = { 0,  1 };
        //    if (key == GLFW_KEY_A || key == GLFW_KEY_LEFT)  d = { -1, 0 };
        //    if (key == GLFW_KEY_D || key == GLFW_KEY_RIGHT) d = { 1, 0 };
        //    if (d != glm::ivec2{ 0,0 }) {
        //        tryMoveDiscreteAABB(d);     // <— ใช้อันใหม่
        //        // อัปเดตทิศเพื่อหมุนตัวละคร
        //        gDir = d;
        //        gMoveAnimStart = gPlayerWorld;
        //        gMoveAnimEnd = glm::vec3(gPlayerEnt.box.center.x, 0, gPlayerEnt.box.center.y);
        //        gMoveT = 0.0f;
        //    }
        //}
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

void handleInputAndMove(GLFWwindow* win, float dt) {
    // อ่านทิศทางจากหลายปุ่มพร้อมกัน
    glm::vec2 in(0.0f);
    if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_RIGHT) == GLFW_PRESS) in.x += 1.0f;
    if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_LEFT) == GLFW_PRESS) in.x -= 1.0f;
    if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_DOWN) == GLFW_PRESS) in.y += 1.0f; // +Z = ลง
    if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_UP) == GLFW_PRESS) in.y -= 1.0f; // -Z = ขึ้น

    if (in.x == 0.0f && in.y == 0.0f) return;

    // นอร์มอลไลซ์ทิศทาง + ตั้งความเร็วหน่วย "ช่องต่อวินาที"
    float len = glm::length(in);
    if (len > 0.0f) in /= len;

    const float speed = 5.0f; // เดิน ~5 ช่อง/วินาที
    glm::vec2 delta = in * speed * dt;

    // --- ถ้า "มีลังอยู่ด้านหน้า" ให้ลองผลักแบบ step ขนาด 1 ช่อง (ยังคงพฤติกรรม Sokoban) ---
    // เฉพาะกรณีที่ผู้เล่นกดเกือบขนานแกนหลัก (กันการผลักเฉียง)
    glm::vec2 axis = (std::abs(in.x) > std::abs(in.y)) ? glm::vec2((in.x > 0) ? 1 : -1, 0) : glm::vec2(0, (in.y > 0) ? 1 : -1);

    // โพรบหาลังด้านหน้า (ใช้ AABB overlap ง่าย ๆ)
    int hitBox = -1;
    {
        AABB probe = gPlayerEnt.box;
        probe.center += axis * 0.6f; // โพรบไปข้างหน้าเล็กน้อย
        for (size_t i = 0; i < gBoxEnts.size(); ++i) {
            glm::vec2 aMin = probe.center - probe.half, aMax = probe.center + probe.half;
            glm::vec2 bMin = gBoxEnts[i].box.center - gBoxEnts[i].box.half, bMax = gBoxEnts[i].box.center + gBoxEnts[i].box.half;
            bool overlap = !(aMax.x < bMin.x || aMin.x > bMax.x || aMax.y < bMin.y || aMin.y > bMax.y);
            if (overlap) { hitBox = (int)i; break; }
        }
    }

    // axis = ทิศผลักหลัก (1,0) หรือ (0,1) ตามที่คุณคำนวณไว้
    glm::vec2 movedBox(0);

    // เคสเจอกล่องข้างหน้า
    if (hitBox >= 0) {
        glm::vec2 pushDelta = axis * (speed * dt);

        if (tryMoveBox((size_t)hitBox, pushDelta, &movedBox) && (movedBox.x != 0 || movedBox.y != 0)) {
            // กล่องขยับได้ -> ผู้เล่นตามไป "เท่าที่กล่องไปจริง"
            glm::vec2 n; moveAndCollide(gPlayerEnt.box, movedBox, gStaticWalls, &n);
        }
        else {
            // กล่องขยับไม่ได้ -> ผู้เล่นไม่ดันซ้อน ให้ slide ด้วยคอมโพเนนต์ที่ไม่ดันเข้ากล่อง
            float vn = glm::dot(delta, axis);            // คอมโพเนนต์ที่ดันเข้ากล่อง
            glm::vec2 deltaSlide = (vn > 0) ? (delta - axis * vn) : delta;  // ตัดเฉพาะถ้ากำลังดันเข้า
            glm::vec2 n; moveAndCollide(gPlayerEnt.box, deltaSlide, gStaticWalls, &n);
        }
    }
    else {
        // ไม่มีลัง -> เดินปกติ (มี slide vs กำแพงอยู่แล้ว)
        glm::vec2 n; moveAndCollide(gPlayerEnt.box, delta, gStaticWalls, &n);
    }

    // กันผู้เล่นซ้อนกล่อง (depenetration สั้น ๆ)
    for (auto& box : gBoxEnts) {
        glm::vec2 aMin = gPlayerEnt.box.center - gPlayerEnt.box.half, aMax = gPlayerEnt.box.center + gPlayerEnt.box.half;
        glm::vec2 bMin = box.box.center - box.box.half, bMax = box.box.center + box.box.half;
        bool overlap = !(aMax.x<bMin.x || aMin.x>bMax.x || aMax.y<bMin.y || aMin.y>bMax.y);
        if (overlap) {
            // ดันผู้เล่นออกจากกล่องทางแกนซ้อนน้อยกว่า
            float ox = std::min(aMax.x - bMin.x, bMax.x - aMin.x);
            float oz = std::min(aMax.y - bMin.y, bMax.y - aMin.y);
            if (ox < oz) gPlayerEnt.box.center.x += (gPlayerEnt.box.center.x < box.box.center.x ? -ox : +ox) * 1.001f;
            else        gPlayerEnt.box.center.y += (gPlayerEnt.box.center.y < box.box.center.y ? -oz : +oz) * 1.001f;
        }
    }


    // อัปเดตทิศเพื่อหมุนโมเดล
    gDir = glm::ivec2((in.x > 0.1f) - (in.x < -0.1f), (in.y > 0.1f) - (in.y < -0.1f));
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

        handleInputAndMove(win, dt);

        // animate movement
       // sync world from collider (ให้อนิเมชันไปทางเดียวกัน)

        glm::vec3 playerGoal = glm::vec3(gPlayerEnt.box.center.x, 0, gPlayerEnt.box.center.y);
        gPlayerWorld = glm::vec3(gPlayerEnt.box.center.x, 0, gPlayerEnt.box.center.y);
        for (auto& e : gBoxEnts) e.world = glm::vec3(e.box.center.x, 0, e.box.center.y);


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
        // boxes (ใช้ตำแหน่งจากฟิสิกส์)
        for (auto& e : gBoxEnts) {
            glm::vec3 pos = e.world + glm::vec3(0, 0.5f, 0);
            if (gAssets.hasBox) {
                glm::mat4 M = glm::translate(glm::mat4(1.0f), pos);
                M = glm::scale(M, glm::vec3(0.15f));
                sh.setMat4("uModel", &M[0][0]);
                sh.setColor("uColor", 0.8f, 0.6f, 0.3f);
                gAssets.box.draw();
            }
            else {
                drawCubeColored(pos, { 1,1,1 }, { 0.7f,0.4f,0.2f });
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
        if (winAABB()) {
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