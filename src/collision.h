#pragma once
#include <glm/glm.hpp>
#include <limits>
#include <vector>

// เราทำคอลิชันบนระนาบ XZ (Y ใช้แค่ความสูงโมเดล)
struct AABB {
    glm::vec2 center;      // (x,z)
    glm::vec2 half;        // ครึ่งกว้าง/ครึ่งยาว
};

struct SweepHit {
    float toi;             // time of impact [0..1], 1 = ไม่ชน
    glm::vec2 normal;      // normal ของผิวที่ชน
};

// 1D ray vs slab helper
static inline bool raySlab(float ro, float rd, float slabMin, float slabMax, float& t0, float& t1){
    if (std::abs(rd) < 1e-8f) return (ro >= slabMin && ro <= slabMax);
    float inv = 1.0f / rd;
    float tNear = (slabMin - ro) * inv;
    float tFar  = (slabMax - ro) * inv;
    if (tNear > tFar) std::swap(tNear, tFar);
    t0 = std::max(t0, tNear);
    t1 = std::min(t1, tFar);
    return t0 <= t1;
}

// Swept AABB (moving box vs static box) 2D
inline SweepHit sweepAABB(const AABB& moving, const glm::vec2& delta, const AABB& target){
    // Enlarge target by moving half extents (Minkowski sum)
    glm::vec2 tmin = target.center - (target.half + moving.half);
    glm::vec2 tmax = target.center + (target.half + moving.half);
    glm::vec2 ro = moving.center; // ray origin
    glm::vec2 rd = delta;         // ray dir

    SweepHit hit; hit.toi = 1.0f; hit.normal = {0,0};

    float t0 = 0.0f, t1 = 1.0f;
    bool okX = raySlab(ro.x, rd.x, tmin.x, tmax.x, t0, t1);
    bool okZ = raySlab(ro.y, rd.y, tmin.y, tmax.y, t0, t1);
    if(!(okX && okZ)) return hit;            // ไม่ตัดกันในช่วง 0..1

    // t0 = first contact
    if(t0 >= 0.0f && t0 <= 1.0f){
        hit.toi = t0;
        // หา normal ตามแกนที่ชนก่อน
        glm::vec2 contact = ro + rd * t0;
        float dxMin = std::abs(contact.x - tmin.x);
        float dxMax = std::abs(contact.x - tmax.x);
        float dzMin = std::abs(contact.y - tmin.y);
        float dzMax = std::abs(contact.y - tmax.y);
        float m = std::min(std::min(dxMin, dxMax), std::min(dzMin, dzMax));
        if(m == dxMin) hit.normal = {-1, 0};
        else if(m == dxMax) hit.normal = { 1, 0};
        else if(m == dzMin) hit.normal = { 0,-1};
        else                hit.normal = { 0, 1};
    }
    return hit;
}

// move and collide vs list (static geometry)
inline float moveAndCollide(AABB& mover, glm::vec2 delta,
                            const std::vector<AABB>& statics,
                            glm::vec2* outNormal=nullptr)
{
    auto overlap2D = [](const AABB& a, const AABB& b, glm::vec2& pushOut){
        glm::vec2 aMin=a.center-a.half, aMax=a.center+a.half;
        glm::vec2 bMin=b.center-b.half, bMax=b.center+b.half;
        if(aMax.x<bMin.x||aMin.x>bMax.x||aMax.y<bMin.y||aMin.y>bMax.y) return false;
        float ox = (aMax.x<bMax.x? aMax.x-bMin.x : bMax.x-aMin.x);
        float oz = (aMax.y<bMax.y? aMax.y-bMin.y : bMax.y-aMin.y);
        if(ox<oz) pushOut = { (a.center.x<b.center.x? -ox: +ox), 0 };
        else      pushOut = { 0, (a.center.y<b.center.y? -oz: +oz) };
        return true;
    };

    // 0) depenetration (เผื่อเริ่มทับกันอยู่)
    for(int k=0;k<3;++k){
        bool any=false; glm::vec2 po;
        for(const auto& s: statics){
            if(overlap2D(mover, s, po)){
                mover.center += po * 1.001f; // ดันออกนิดเดียว
                any=true;
            }
        }
        if(!any) break;
    }

    glm::vec2 nAccum{0,0};
    glm::vec2 remain = delta;

    for(int iter=0; iter<4 && (std::abs(remain.x)+std::abs(remain.y))>1e-6f; ++iter){
        float bestToi = 1.0f; glm::vec2 bestN{0,0};
        for(const auto& s : statics){
            SweepHit h = sweepAABB(mover, remain, s);
            if(h.toi < bestToi){ bestToi=h.toi; bestN=h.normal; }
        }

        // เดินถึงจุดชน (หรือทั้งช่วงถ้าไม่ชน)
        mover.center += remain * bestToi;

        if(bestToi < 1.0f - 1e-5f){
            // มีชนในช่วงนี้
            float vn = remain.x*bestN.x + remain.y*bestN.y; // ความเร็วตั้งฉากกับผิว
            // เหลือระยะ (สเกล) หลังชน
            glm::vec2 leftoverDir = remain * (1.0f - bestToi);

            if(vn > 0.0f){
                // วิ่ง "เข้า" ผนัง → ตัดคอมโพเนนต์ตั้งฉากออก = slide
                glm::vec2 slid = leftoverDir - bestN * vn * (1.0f - bestToi);
                remain = slid;
            }else{
                // วิ่ง "ออกจาก" ผนังอยู่แล้ว → อย่าตัดออก (ไม่งั้นติด)
                remain = leftoverDir;
            }

            nAccum = bestN;
            // ขยับออกจากผิวจิ๋ว ๆ ป้องกัน re-hit ที่ toi=0
            mover.center += bestN * 1e-4f;
        }else{
            // ไม่ชนแล้ว
            remain = {0,0};
            break;
        }
    }

    if(outNormal) *outNormal = nAccum;
    return 0.0f; // (ไม่จำเป็นต้องใช้ค่านี้ในตอนนี้)
}
