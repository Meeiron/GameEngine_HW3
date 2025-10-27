#pragma once
#include "mesh.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <filesystem>
#include <iostream>

struct Model {
    std::vector<Mesh> meshes;
    bool loaded=false;
    std::filesystem::path baseDir;

    bool load(const std::string& path){
        std::cerr << "Loading model: " << path << std::endl;
        Assimp::Importer imp;
        const aiScene* scene = imp.ReadFile(path,
            aiProcess_Triangulate |
            aiProcess_GenSmoothNormals |
            aiProcess_JoinIdenticalVertices |
            aiProcess_ImproveCacheLocality |
            aiProcess_PreTransformVertices |
            aiProcess_CalcTangentSpace
        );
        if(!scene || !scene->mRootNode){
            std::cerr << "Assimp error: " << imp.GetErrorString() << "\n";
            loaded=false; return false;
        }
        baseDir = std::filesystem::path(path).parent_path();
        processNode(scene->mRootNode, scene);
        for(auto& m : meshes) m.upload();
        loaded=true;
        return true;
    }

    void draw() const {
        for(auto& m : meshes) m.draw();
    }

private:
    void processNode(aiNode* node, const aiScene* scene){
        for(unsigned i=0;i<node->mNumMeshes;++i){
            aiMesh* a = scene->mMeshes[node->mMeshes[i]];
            meshes.push_back(processMesh(a, scene));
        }
        for(unsigned i=0;i<node->mNumChildren;++i){
            processNode(node->mChildren[i], scene);
        }
    }
    Mesh processMesh(aiMesh* a, const aiScene* scene){
        Mesh m;
        m.vertices.reserve(a->mNumVertices);
        for(unsigned i=0;i<a->mNumVertices;++i){
            Vertex v{};
            v.pos = {a->mVertices[i].x, a->mVertices[i].y, a->mVertices[i].z};
            if(a->HasNormals()){
                v.normal = {a->mNormals[i].x, a->mNormals[i].y, a->mNormals[i].z};
            } else {
                v.normal = {0,1,0};
            }
            if(a->mTextureCoords[0]){
                v.uv = {a->mTextureCoords[0][i].x, a->mTextureCoords[0][i].y};
            } else {
                v.uv = {0,0};
            }
            m.vertices.push_back(v);
        }
        for(unsigned i=0;i<a->mNumFaces;++i){
            aiFace f = a->mFaces[i];
            for(unsigned j=0;j<f.mNumIndices;++j) m.indices.push_back(f.mIndices[j]);
        }
        // Textures optional; keeping simple: not loading materials for brevity
        m.hasTexture = false;
        return m;
    }
};