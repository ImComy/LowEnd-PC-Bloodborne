// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

// Pure, renderer-independent layout and conservative cut-decoration geometry.
namespace BbAtlasStyle {
struct Point { float x, z; };
struct Line { Point a, b; uint32_t wear=0; };
struct Triangle { std::array<Point,3> p; };
struct Box { float x,y,w,h; };
struct Layout {
    static constexpr float width=1280, height=720;
    float scale, x, y;
    explicit Layout(float w,float h,float sx=0,float sy=0)
        :scale(std::max(.01f,std::min(w/width,h/height))),
         x(sx+(w-width*scale)*.5f),y(sy+(h-height*scale)*.5f){}
    Point At(float px,float py) const {return {x+px*scale,y+py*scale};}
    Box Rect(float px,float py,float w,float h)const{return {x+px*scale,y+py*scale,w*scale,h*scale};}
    // Leave room for the subtitle's descenders before any map geometry.
    Box Canvas()const{return Rect(230,114,835,538);}
    static constexpr float edge_fade=34;
    static constexpr float frame_left=10,frame_top=8,frame_right=1270,frame_bottom=710;
    static constexpr float title_top=16,title_size=51,title_min=27,title_width=1040,title_region_h=47;
    static constexpr float header_rule=67,header_left=320,header_right=960,subtitle_top=77,subtitle_size=29,subtitle_min=18,subtitle_width=980,subtitle_region_h=30;
    static constexpr float footer_rule=659,footer_baseline=688,footer_size=18;
    static constexpr float compass_x=77,compass_y=105,compass_size=73,player_size=35;
    static constexpr float legend_x=1163,legend_y=524,legend_w=18,legend_h=104;
    static constexpr float key_x=1126,key_y=644,key_w=116,key_h=54;
    static constexpr float footer_x=152,footer_w=964,footer_slots=3;
    Box TravelDialog()const{return Rect((width-350)*.5f,(height-168)*.5f,350,168);}
    // Every enabled control uses the same row; disabling travel leaves no holes.
    Box FooterSlot(int index,int count)const{return Rect(footer_x+footer_w*index/count,666,footer_w/count,35);}
    static bool LampOnMap(bool same_area,bool discovered){return same_area&&discovered;}
};
inline float Cross(Point a,Point b){return a.x*b.z-a.z*b.x;}
inline Point Sub(Point a,Point b){return {a.x-b.x,a.z-b.z};}
inline Point Mix(Point a,Point b,float t){return {a.x+(b.x-a.x)*t,a.z+(b.z-a.z)*t};}

// Exact segment/convex-polygon clipping. Expanding the floor by epsilon avoids
// strokes landing on its boundary through floating-point roundoff.
template<size_t N> bool Clip(const std::array<Point,N>& polygon,Point a,Point b,float& lo,float& hi,float epsilon=0){
    float area=0;for(size_t i=0;i<N;++i)area+=Cross(polygon[i],polygon[(i+1)%N]);
    if(std::abs(area)<1.e-8f)return false;
    const float sign=area>0?1.f:-1.f;const auto d=Sub(b,a);lo=0;hi=1;
    for(size_t i=0;i<N;++i){
        const auto e=Sub(polygon[(i+1)%N],polygon[i]);
        const float edge_length=std::hypot(e.x,e.z);
        const float start=sign*Cross(e,Sub(a,polygon[i]))+epsilon*edge_length;
        const float slope=sign*Cross(e,d);
        if(std::abs(slope)<1.e-9f){if(start<0)return false;continue;}
        const float t=-start/slope;
        if(slope>0)lo=std::max(lo,t);else hi=std::min(hi,t);
        if(lo>hi)return false;
    }
    return hi>=0&&lo<=1;
}
inline int64_t CellKey(int x,int z){return int64_t((uint64_t(uint32_t(x))<<32)|uint32_t(z));}
struct FloorIndex {
    static constexpr float cell=8;
    std::vector<Triangle> triangles;
    std::unordered_map<int64_t,std::vector<uint32_t>> cells;
    std::vector<uint32_t> broad,marks;
    uint32_t serial=0;
    void Build(std::vector<Triangle> geometry){
        triangles=std::move(geometry);marks.assign(triangles.size(),0);
        for(uint32_t i=0;i<triangles.size();++i){const auto& t=triangles[i].p;
            const int x0=int(std::floor(std::min({t[0].x,t[1].x,t[2].x})/cell));
            const int x1=int(std::floor(std::max({t[0].x,t[1].x,t[2].x})/cell));
            const int z0=int(std::floor(std::min({t[0].z,t[1].z,t[2].z})/cell));
            const int z1=int(std::floor(std::max({t[0].z,t[1].z,t[2].z})/cell));
            if(int64_t(x1-x0+1)*(z1-z0+1)>512){broad.push_back(i);continue;}
            for(int x=x0;x<=x1;++x)for(int z=z0;z<=z1;++z)cells[CellKey(x,z)].push_back(i);
        }
    }
    template<class F> void Query(Point a,Point b,F fn){
        if(++serial==0){std::fill(marks.begin(),marks.end(),0);serial=1;}
        auto visit=[&](uint32_t id){if(marks[id]!=serial){marks[id]=serial;fn(triangles[id]);}};
        const int x0=int(std::floor((std::min(a.x,b.x)-.02f)/cell)),x1=int(std::floor((std::max(a.x,b.x)+.02f)/cell));
        const int z0=int(std::floor((std::min(a.z,b.z)-.02f)/cell)),z1=int(std::floor((std::max(a.z,b.z)+.02f)/cell));
        for(int x=x0;x<=x1;++x)for(int z=z0;z<=z1;++z){const auto it=cells.find(CellKey(x,z));if(it!=cells.end())for(auto id:it->second)visit(id);}
        for(auto id:broad)visit(id);
    }
    bool Contains(Point p){bool hit=false;Query(p,p,[&](const Triangle& t){float lo,hi;if(Clip(t.p,p,p,lo,hi,.002f))hit=true;});return hit;}
    std::vector<Line> Outside(Point a,Point b,uint32_t wear){
        std::vector<std::pair<float,float>> occupied;
        Query(a,b,[&](const Triangle& t){float lo,hi;if(Clip(t.p,a,b,lo,hi,.012f))occupied.emplace_back(std::max(0.f,lo),std::min(1.f,hi));});
        std::sort(occupied.begin(),occupied.end());std::vector<Line> result;float begin=0;
        auto emit=[&](float lo,float hi){if(hi-lo>.001f)result.push_back({Mix(a,b,lo),Mix(a,b,hi),wear});};
        for(const auto& interval:occupied){if(interval.first>begin)emit(begin,interval.first);begin=std::max(begin,interval.second);}
        if(begin<1)emit(begin,1);return result;
    }
};
inline uint32_t Hash(uint32_t n){n^=n>>16;n*=0x7feb352dU;n^=n>>15;n*=0x846ca68bU;return n^(n>>16);}
struct CutCache {
    static constexpr int lod_count=18;
    bool ready=false;
    FloorIndex floors;
    // next_wall/next_piece: where a Step that ran out of time resumes.
    struct Lod {bool built=false;std::vector<Line> strokes;size_t next_wall=0;int next_piece=0;};
    std::vector<std::array<Lod,lod_count>> layers;
    void Build(std::vector<Triangle> floor_geometry,size_t layer_count){floors.Build(std::move(floor_geometry));layers.resize(layer_count);ready=true;}
    // The diagonal's perpendicular pitch is spacing / sqrt(2). A 1.5x
    // hierarchy keeps nearest-LOD pitch in 4.49..6.74 design pixels.
    static float Spacing(int level){return .125f*std::pow(1.5f,float(std::clamp(level,0,lod_count-1)));}
    static int Level(float zoom,float scale){return std::clamp(int(std::lround(std::log(5.5f*1.41421356237f*scale/std::max(.0001f,zoom*.125f))/std::log(1.5f))),0,lod_count-1);}
    const std::vector<Line>& Get(size_t layer,int level,const std::vector<Line>& walls){
        auto& lod=layers[layer][std::clamp(level,0,lod_count-1)];if(!lod.built)Step(layer,level,walls,std::numeric_limits<double>::infinity());
        return lod.strokes;
    }
    bool Built(size_t layer,int level)const{return layers[layer][std::clamp(level,0,lod_count-1)].built;}
    // The finished level closest to `level`, or -1 while none is.
    int NearestBuilt(size_t layer,int level)const{
        level=std::clamp(level,0,lod_count-1);
        for(int d=0;d<lod_count;++d){if(level-d>=0&&Built(layer,level-d))return level-d;if(level+d<lod_count&&Built(layer,level+d))return level+d;}
        return -1;
    }
    // Builds the level for about budget_ms, at least one wall piece per call, resuming where
    // the last call stopped (the strokes come out exactly as one uninterrupted Get makes them).
    // Pass the same walls each time. True once the level is complete.
    bool Step(size_t layer,int level,const std::vector<Line>& walls,double budget_ms){
        auto& lod=layers[layer][std::clamp(level,0,lod_count-1)];if(lod.built)return true;
        const float spacing=Spacing(level);
        const bool timed=budget_ms<std::numeric_limits<double>::infinity();
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::duration<double,std::milli>(timed?budget_ms:0);
        bool first=true;
        // Supported wall bands only: subdivide to bound the distance from the
        // support probe, never extrapolate a band beyond either wall endpoint.
        for(;lod.next_wall<walls.size();++lod.next_wall,lod.next_piece=0){const size_t wi=lod.next_wall;const auto w=walls[wi];auto d=Sub(w.b,w.a);const float length=std::hypot(d.x,d.z);if(length<.03f||length>10000)continue;
            const Point normal{-d.z/length,d.x/length};const int pieces=std::max(1,int(std::ceil(length/1.25f)));
            for(;lod.next_piece<pieces;){
                if(!first&&timed&&std::chrono::steady_clock::now()>=deadline)return false;
                first=false;const int piece=lod.next_piece++;
                const auto a=Mix(w.a,w.b,float(piece)/pieces),b=Mix(w.a,w.b,float(piece+1)/pieces),mid=Mix(a,b,.5f);
                // With lines alone we cannot infer a building's solid interior.
                // A narrow .65m band near a proven floor boundary is all we draw.
                bool supported=false;for(float side:{-.35f,.35f})if(floors.Contains({mid.x+normal.x*side,mid.z+normal.z*side}))supported=true;
                if(!supported)continue;
                const float band=.65f;const std::array<Point,4> polygon={Point{a.x+normal.x*band,a.z+normal.z*band},Point{b.x+normal.x*band,b.z+normal.z*band},Point{b.x-normal.x*band,b.z-normal.z*band},Point{a.x-normal.x*band,a.z-normal.z*band}};
                float low=polygon[0].x-polygon[0].z,high=low;for(auto p:polygon){low=std::min(low,p.x-p.z);high=std::max(high,p.x-p.z);}
                // Constant x-z lines are world anchored, including LOD phase.
                const int first=int(std::ceil(low/spacing)),last=int(std::floor(high/spacing));
                for(int n=first;n<=last;++n){const float phase=n*spacing;const Point origin{mid.x,(mid.x-phase)},start{origin.x-4,origin.z-4},end{origin.x+4,origin.z+4};float lo,hi;
                    if(!Clip(polygon,start,end,lo,hi))continue;
                    const uint32_t wear=Hash(uint32_t(n)^Hash(uint32_t(wi*71+piece)));
                    const auto cuts=floors.Outside(Mix(start,end,lo),Mix(start,end,hi),wear);
                    lod.strokes.insert(lod.strokes.end(),cuts.begin(),cuts.end());
                }
            }
        }
        lod.built=true;return true;
    }
};
} // namespace BbAtlasStyle
