#include "Loader.hpp"

#include "Animation.hpp"
#include "Material.hpp"
#include "StaticMesh.hpp"
#include "StaticModel.hpp"
#include "AnimatedMesh.hpp"
#include "AnimatedModel.hpp"
#include "Texture.hpp"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <glm/ext.hpp>
#include <png.h>
#include <queue>
#include <vector>
#include <set>
#include <unordered_map>
#include <iostream>
#include <fstream>

namespace ne
{

  namespace
  {
    uint8_t readU8(std::istream &in)
    {
      return in.get();
    }

    uint32_t readU32(std::istream &in)
    {
      uint32_t ret;
      in.read((char*)&ret, 4);
      return ret;
    }

    float readF32(std::istream &in)
    {
      float ret;
      in.read((char*)&ret, 4);
      return ret;
    }

    double readF64(std::istream &in)
    {
      double ret;
      in.read((char*)&ret, 8);
      return ret;
    }

    void readBytes(std::istream &in, char* buf, size_t num)
    {
      in.read(buf, num);
    }

    glm::mat4 readM44(std::istream &in)
    {
      glm::mat4 ret;
      readBytes(in, (char*)&ret[0][0], 16 * 4);
      return ret;
    }
  }

  Loader::Loader()
  {
  }

  Loader::~Loader()
  {
    for(auto it : m_textures)
      delete it.second;
    for(auto it : m_staticModels)
      delete it.second;
    for(auto it : m_animatedModels)
      delete it.second;
  }

  StaticMesh* Loader::LoadStaticMesh(const aiMesh* mesh)
  {
    std::vector<GLfloat> data;
    for(GLuint i = 0; i < mesh->mNumVertices; ++i)
    {
      data.push_back(mesh->mVertices[i].x);
      data.push_back(mesh->mVertices[i].y);
      data.push_back(mesh->mVertices[i].z);
      if(mesh->mTextureCoords[0])
      {
        data.push_back(mesh->mTextureCoords[0][i].x);
        data.push_back(mesh->mTextureCoords[0][i].y);
      }
      else
      {
        data.push_back(0.0f);
        data.push_back(0.0f);
      }
      data.push_back(mesh->mNormals[i].x);
      data.push_back(mesh->mNormals[i].y);
      data.push_back(mesh->mNormals[i].z);
    }

    std::vector<GLuint> indices;
    for(GLuint i = 0; i < mesh->mNumFaces; ++i)
    {
      for(GLuint j = 0; j < mesh->mFaces[i].mNumIndices; ++j)
      {
        indices.push_back(mesh->mFaces[i].mIndices[j]);
      }
    }

    StaticMesh* pMesh = new StaticMesh();
    pMesh->m_iNumTris = data.size() / 24; //(8 floats per vertex, 3 vertices per tri)
    pMesh->m_iNumIndices = indices.size();
    pMesh->m_iStride = 8 * sizeof(GLfloat);
    pMesh->m_iOffPos = 0 * sizeof(GLfloat);
    pMesh->m_iOffUV = 3 * sizeof(GLfloat);
    pMesh->m_iOffNormal = 5 * sizeof(GLfloat);

    glGenVertexArrays(1, &pMesh->m_vaoConfig);
    glGenBuffers(1, &pMesh->m_vboVertices);
    glGenBuffers(1, &pMesh->m_vboIndices);

    glBindVertexArray(pMesh->m_vaoConfig);

    glBindBuffer(GL_ARRAY_BUFFER, pMesh->m_vboVertices);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(GLfloat), &data[0], GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pMesh->m_vboIndices);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), &indices[0], GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, pMesh->m_iStride, (void*)pMesh->m_iOffPos);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, pMesh->m_iStride, (void*)pMesh->m_iOffUV);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, pMesh->m_iStride, (void*)pMesh->m_iOffNormal);

    glBindVertexArray(0);

    return pMesh;
  }

  void Loader::ProcessModelNode(StaticModel* model, const aiScene* scene, const aiNode* node)
  {
    for(GLuint i = 0; i < node->mNumMeshes; ++i)
    {
      const aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
      StaticMesh* heMesh = LoadStaticMesh(mesh);
      model->m_meshes.push_back(heMesh);

      const aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
      Texture *lambert = nullptr;
      Texture *normal = nullptr;
      if(material->GetTextureCount(aiTextureType_DIFFUSE) > 0)
      {
        aiString str;
        material->GetTexture(aiTextureType_DIFFUSE, 0, &str);
        std::string path(str.C_Str());
        lambert = LoadTexture(path, TextureFormat::Color);
      }
      //TODO - don't cheat to load normals
      if(material->GetTextureCount(aiTextureType_AMBIENT) > 0)
      {
        aiString str;
        material->GetTexture(aiTextureType_AMBIENT, 0, &str);
        std::string path(str.C_Str());
        normal = LoadTexture(path, TextureFormat::Normal);
      }
      model->m_materials.push_back(new Material(lambert, normal));
    }
    for(GLuint i = 0; i < node->mNumChildren; ++i)
    {
      ProcessModelNode(model, scene, node->mChildren[i]);
    }
  }

  StaticModel* Loader::LoadStaticModel(const std::string &path)
  {
    //Check cache
    {
      auto it = m_staticModels.find(path);
      if(it != m_staticModels.end()) {
        return it->second;
      }
    }

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals |
        aiProcess_PreTransformVertices | aiProcess_SplitLargeMeshes |
        aiProcess_RemoveRedundantMaterials | aiProcess_GenUVCoords);

    if(!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
      return nullptr;

    StaticModel* model = new StaticModel();
    ProcessModelNode(model, scene, scene->mRootNode);

    m_staticModels[path] = model;
    return model;
  }

  AnimatedModel* Loader::LoadAnimatedModel(const std::string &path)
  {
    return nullptr;
  }

  Skeleton* Loader::LoadSkeleton(const std::string& path)
  {
    std::ifstream in(path);

    if(!in.good())
      return nullptr;

    const size_t boneCount = readU32(in);

    Skeleton* skel = new Skeleton;
    for(size_t i = 0; i < boneCount; ++i)
    {
      Bone b;
      b.id = readU8(in);
      b.localPos.x = readF32(in);
      b.localPos.y = readF32(in);
      b.localPos.z = readF32(in);
      b.localRot[0] = readF32(in);
      b.localRot[1] = readF32(in);
      b.localRot[2] = readF32(in);
      b.localRot[3] = readF32(in);
      b.invTransform = readM44(in);
      std::string name(readU8(in), ' ');
      readBytes(in, &name[0], name.length());
      b.name = name;

      const uint8_t numChildren = readU8(in);
      for(size_t j = 0; j < numChildren; ++j)
        b.childIds.push_back(readU8(in));

      skel->bones.push_back(b);
    }

    return skel;
  }

  StaticMesh* Loader::LoadBakedStaticMesh(const std::string& path)
  {
    std::ifstream in(path);

    if(!in.good())
      return nullptr;

    const char flags = readU8(in);

    //Check whether this is a static mesh
    if(flags & 1)
      return nullptr;

    const size_t numVerts = readU32(in);
    const size_t numIndices = readU32(in);

    std::vector<GLfloat> vertexData(16 * numVerts);
    std::vector<GLuint> indexData(numIndices);

    //Load all the vertex data in one go
    readBytes(in, (char*)&vertexData[0], 8 * sizeof(GLfloat) * numVerts);

    //Load all the index data in one go
    readBytes(in, (char*)&indexData[0], 4 * numIndices);

    StaticMesh* pMesh = new StaticMesh();
    pMesh->m_iNumTris = numVerts / 3;
    pMesh->m_iNumIndices = numIndices;
    pMesh->m_iStride = 8 * sizeof(GLfloat);
    pMesh->m_iOffPos = 0 * sizeof(GLfloat);
    pMesh->m_iOffNormal = 3 * sizeof(GLfloat);
    pMesh->m_iOffUV = 6 * sizeof(GLfloat);

    glGenVertexArrays(1, &pMesh->m_vaoConfig);
    glGenBuffers(1, &pMesh->m_vboVertices);
    glGenBuffers(1, &pMesh->m_vboIndices);

    glBindVertexArray(pMesh->m_vaoConfig);

    glBindBuffer(GL_ARRAY_BUFFER, pMesh->m_vboVertices);
    glBufferData(GL_ARRAY_BUFFER, vertexData.size() * sizeof(GLfloat), &vertexData[0], GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pMesh->m_vboIndices);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexData.size() * sizeof(GLuint), &indexData[0], GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);

    glBindBuffer(GL_ARRAY_BUFFER, pMesh->m_vboVertices);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, pMesh->m_iStride, (void*)pMesh->m_iOffPos);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, pMesh->m_iStride, (void*)pMesh->m_iOffUV);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, pMesh->m_iStride, (void*)pMesh->m_iOffNormal);

    glBindVertexArray(0);

    return pMesh;
  }

  AnimatedMesh* Loader::LoadAnimatedMesh(const std::string& path)
  {
    std::ifstream in(path);

    if(!in.good())
      return nullptr;

    const char flags = readU8(in);

    //Check whether this is a skeletel mesh
    if(!flags & 1)
      return nullptr;

    const size_t numVerts = readU32(in);
    const size_t numIndices = readU32(in);

    std::vector<GLfloat> vertexData(16 * numVerts);
    std::vector<GLuint> indexData(numIndices);

    //Load all the vertex data in one go
    readBytes(in, (char*)&vertexData[0], 16 * sizeof(GLfloat) * numVerts);

    //Load all the index data in one go
    readBytes(in, (char*)&indexData[0], 4 * numIndices);

    AnimatedMesh* pMesh = new AnimatedMesh();
    pMesh->m_iNumTris = numVerts / 3;
    pMesh->m_iNumIndices = numIndices;
    pMesh->m_iStride = 16 * sizeof(GLfloat);
    pMesh->m_iOffPos = 0 * sizeof(GLfloat);
    pMesh->m_iOffNormal = 3 * sizeof(GLfloat);
    pMesh->m_iOffUV = 6 * sizeof(GLfloat);
    pMesh->m_iOffBoneWeights = 8 * sizeof(GLfloat);
    pMesh->m_iOffBoneIds = 12 * sizeof(GLfloat);

    glGenVertexArrays(1, &pMesh->m_vaoConfig);
    glGenBuffers(1, &pMesh->m_vboVertices);
    glGenBuffers(1, &pMesh->m_vboIndices);

    glBindVertexArray(pMesh->m_vaoConfig);

    glBindBuffer(GL_ARRAY_BUFFER, pMesh->m_vboVertices);
    glBufferData(GL_ARRAY_BUFFER, vertexData.size() * sizeof(GLfloat), &vertexData[0], GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pMesh->m_vboIndices);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexData.size() * sizeof(GLuint), &indexData[0], GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glEnableVertexAttribArray(4);

    glBindBuffer(GL_ARRAY_BUFFER, pMesh->m_vboVertices);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, pMesh->m_iStride, (void*)pMesh->m_iOffPos);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, pMesh->m_iStride, (void*)pMesh->m_iOffNormal);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, pMesh->m_iStride, (void*)pMesh->m_iOffUV);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, pMesh->m_iStride, (void*)pMesh->m_iOffBoneWeights);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, pMesh->m_iStride, (void*)pMesh->m_iOffBoneIds);

    glBindVertexArray(0);

    return pMesh;
  }

  Animation* Loader::LoadAnimation(const std::string& path)
  {
    std::ifstream in(path);

    if(!in.good())
      return nullptr;

    const size_t numChannels = readU32(in);

    Animation* anim = new Animation();

    for(size_t i = 0; i < numChannels; ++i)
    {
      AnimationChannel channel;
      channel.boneId = readU8(in);

      const size_t numKeys = readU32(in);
      for(size_t k = 0; k < numKeys; ++k)
      {
        Keyframe keyframe;
        keyframe.time = readF64(in);

        //Update the duration of the animation
        anim->m_duration = std::max(anim->m_duration, keyframe.time);

        keyframe.position.x = readF32(in);
        keyframe.position.y = readF32(in);
        keyframe.position.z = readF32(in);

        keyframe.rotation.x = readF32(in);
        keyframe.rotation.y = readF32(in);
        keyframe.rotation.z = readF32(in);
        keyframe.rotation.w = readF32(in);

        channel.keyframes.push_back(keyframe);
      }

      anim->m_channels.push_back(channel);
    }

    return anim;
  }

  Texture* Loader::LoadTexture(const std::string &path, enum TextureFormat format)
  {
    {
      auto it = m_textures.find(path);
      if(it != m_textures.end()) {
        return it->second;
      }
    }

    FILE *fp = fopen(path.c_str(), "rb");

    if(!fp)
      return nullptr;

    png_structp pPNG = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if(!pPNG)
    {
      fclose(fp);
      return nullptr;
    }

    png_infop pInfo = png_create_info_struct(pPNG);
    if(!pInfo)
    {
      fclose(fp);
      png_destroy_read_struct(&pPNG, NULL, NULL);
      return nullptr;
    }

    png_init_io(pPNG, fp);
    png_read_png(pPNG, pInfo, PNG_TRANSFORM_STRIP_16 | PNG_TRANSFORM_STRIP_ALPHA | PNG_TRANSFORM_GRAY_TO_RGB | PNG_TRANSFORM_PACKING | PNG_TRANSFORM_EXPAND, NULL);
    png_uint_32 width, height;
    png_get_IHDR(pPNG, pInfo, &width, &height, NULL, NULL, NULL, NULL, NULL);
    unsigned int bytesPerRow = png_get_rowbytes(pPNG, pInfo);
    std::vector<char> data(bytesPerRow*height);
    png_bytepp rowPointers = png_get_rows(pPNG, pInfo);

    for (unsigned int i = 0; i < height; i++)
      memcpy(&data[0]+(bytesPerRow * (height-1-i)), rowPointers[i], bytesPerRow);

    png_destroy_read_struct(&pPNG, &pInfo, NULL);
    fclose(fp);

    GLuint internalFormat;
    switch(format)
    {
      case TextureFormat::Color:
        internalFormat = GL_SRGB8;
        break;

      case TextureFormat::Normal:
        internalFormat = GL_RGB8;
        break;

      case TextureFormat::Map:
        internalFormat = GL_R8;
        break;

      default:
        internalFormat = GL_RGB8;
    }

    Texture *pTex = new Texture();
    pTex->m_width = width;
    pTex->m_height = width;
    glGenTextures(1, &pTex->m_glTexture);
    glBindTexture(GL_TEXTURE_2D, pTex->m_glTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, &data[0]);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_textures[path] = pTex;
    return pTex;
  }

  Texture* Loader::GenerateBlankNormal()
  {
    const int size = 8;
    //Create a solid 8 * 8 blue texture (+Z normal)
    std::vector<unsigned char> pixels(size*size*3, 0);
    for(size_t i = 0; i < pixels.size(); i += 3)
    {
      pixels[i+0] = 128;
      pixels[i+1] = 128;
      pixels[i+2] = 255;
    }

    Texture *pTex = new Texture();
    pTex->m_width = size;
    pTex->m_height = size;
    glGenTextures(1, &pTex->m_glTexture);
    glBindTexture(GL_TEXTURE_2D, pTex->m_glTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, &pixels[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    return pTex;
  }

  Texture* Loader::GenerateBlankMap(unsigned char value)
  {
    const int size = 8;
    //Create a solid 8 * 8 texture
    std::vector<unsigned char> pixels(size*size*3, 0);
    for(size_t i = 0; i < pixels.size(); ++i)
    {
      pixels[i] = value;
    }

    Texture *pTex = new Texture();
    pTex->m_width = size;
    pTex->m_height = size;
    glGenTextures(1, &pTex->m_glTexture);
    glBindTexture(GL_TEXTURE_2D, pTex->m_glTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, &pixels[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    return pTex;
  }

  Texture* Loader::GeneratePurpleCheques()
  {
    const int size = 8;
    //Create a 8x8 purple chequerboard texture
    std::vector<unsigned char> pixels(size*size*3, 0);
    for(size_t i = 0; i < pixels.size(); i += 3)
    {
      size_t x = i % (8*3);
      size_t y = i / (8*3);
      if((x % 2) ^ (y % 2))
      {
        pixels[i+0] = 255;
        pixels[i+2] = 255;
      }
    }

    Texture *pTex = new Texture();
    pTex->m_width = size;
    pTex->m_height = size;
    glGenTextures(1, &pTex->m_glTexture);
    glBindTexture(GL_TEXTURE_2D, pTex->m_glTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, &pixels[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    return pTex;
  }

  StaticMesh* Loader::GeneratePlane()
  {
    StaticMesh *pMesh = new StaticMesh();
    pMesh->m_iNumTris = 2;
    pMesh->m_iStride = 8 * sizeof(GLfloat);
    pMesh->m_iOffPos = 0 * sizeof(GLfloat);
    pMesh->m_iOffUV = 3 * sizeof(GLfloat);
    pMesh->m_iOffNormal = 5 * sizeof(GLfloat);

    std::vector<GLfloat> data = {
      -1,-1,0, 0,0, 0,0,1,
       1, 1,0, 1,1, 0,0,1,
      -1, 1,0, 0,1, 0,0,1,
      -1,-1,0, 0,0, 0,0,1,
       1,-1,0, 1,0, 0,0,1,
       1, 1,0, 1,1, 0,0,1
    };

    glGenVertexArrays(1, &pMesh->m_vaoConfig);
    glGenBuffers(1, &pMesh->m_vboVertices);

    glBindVertexArray(pMesh->m_vaoConfig);

    glBindBuffer(GL_ARRAY_BUFFER, pMesh->m_vboVertices);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(GLfloat), &data[0], GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, pMesh->m_iStride, (void*)pMesh->m_iOffPos);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, pMesh->m_iStride, (void*)pMesh->m_iOffUV);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, pMesh->m_iStride, (void*)pMesh->m_iOffNormal);

    glBindVertexArray(0);

    return pMesh;
  }

  StaticMesh* Loader::GenerateCube()
  {
    StaticMesh *pMesh = new StaticMesh();

    pMesh->m_iNumTris = 12;
    pMesh->m_iStride = 8 * sizeof(GLfloat);
    pMesh->m_iOffPos = 0 * sizeof(GLfloat);
    pMesh->m_iOffUV = 3 * sizeof(GLfloat);
    pMesh->m_iOffNormal = 5 * sizeof(GLfloat);

    std::vector<GLfloat> data = {
      -1,-1,-1, 0,0, 0,0,-1,
      -1, 1,-1, 0,1, 0,0,-1,
       1, 1,-1, 1,1, 0,0,-1,
      -1,-1,-1, 0,0, 0,0,-1,
       1, 1,-1, 1,1, 0,0,-1,
       1,-1,-1, 1,0, 0,0,-1,

      -1,-1, 1, 0,0, 0,0,1,
       1, 1, 1, 1,1, 0,0,1,
      -1, 1, 1, 0,1, 0,0,1,
      -1,-1, 1, 0,0, 0,0,1,
       1,-1, 1, 1,0, 0,0,1,
       1, 1, 1, 1,1, 0,0,1,

      -1,-1,-1, 0,0, 0,-1,0,
       1,-1, 1, 1,1, 0,-1,0,
      -1,-1, 1, 0,1, 0,-1,0,
      -1,-1,-1, 0,0, 0,-1,0,
       1,-1,-1, 1,0, 0,-1,0,
       1,-1, 1, 1,1, 0,-1,0,

      -1, 1,-1, 0,0, 0,1,0,
      -1, 1, 1, 0,1, 0,1,0,
       1, 1, 1, 1,1, 0,1,0,
      -1, 1,-1, 0,0, 0,1,0,
       1, 1, 1, 1,1, 0,1,0,
       1, 1,-1, 1,0, 0,1,0,

      -1,-1,-1, 0,0, -1,0,0,
      -1,-1, 1, 0,1, -1,0,0,
      -1, 1, 1, 1,1, -1,0,0,
      -1,-1,-1, 0,0, -1,0,0,
      -1, 1, 1, 1,1, -1,0,0,
      -1, 1,-1, 1,0, -1,0,0,

       1,-1,-1, 0,0, 1,0,0,
       1, 1, 1, 1,1, 1,0,0,
       1,-1, 1, 0,1, 1,0,0,
       1,-1,-1, 0,0, 1,0,0,
       1, 1,-1, 1,0, 1,0,0,
       1, 1, 1, 1,1, 1,0,0,
    };

    glGenVertexArrays(1, &pMesh->m_vaoConfig);
    glGenBuffers(1, &pMesh->m_vboVertices);

    glBindVertexArray(pMesh->m_vaoConfig);

    glBindBuffer(GL_ARRAY_BUFFER, pMesh->m_vboVertices);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(GLfloat), &data[0], GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, pMesh->m_iStride, (void*)pMesh->m_iOffPos);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, pMesh->m_iStride, (void*)pMesh->m_iOffUV);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, pMesh->m_iStride, (void*)pMesh->m_iOffNormal);

    glBindVertexArray(0);

    return pMesh;
  }

  StaticMesh* Loader::GenerateSphere()
  {
    std::vector<GLfloat> verts;
    std::vector<GLuint> indices;

    const int numRows = 12;
    const int numCols = 18;
    const int firstRingBaseIndex = 2;
    const int lastRingBaseIndex = firstRingBaseIndex + (numRows-2) * numCols;
    const float colInc = 2.0 * glm::pi<float>() / numCols;

    //Poles
    verts.push_back(0.0); //x
    verts.push_back(0.5); //y
    verts.push_back(0.0); //z
    verts.push_back(0.0); //u
    verts.push_back(0.0); //v
    verts.push_back(0.0); //normal.x
    verts.push_back(1.0); //normal.y
    verts.push_back(0.0); //normal.z

    verts.push_back(0.0); //x
    verts.push_back(-0.5); //y
    verts.push_back(0.0); //z
    verts.push_back(0.0); //u
    verts.push_back(0.0); //v
    verts.push_back(0.0); //normal.x
    verts.push_back(-1.0); //normal.y
    verts.push_back(0.0); //normal.z

    for(int i = 1; i < numRows - 0; ++i)
    {
      const float y = 0.5 * cos(i / glm::pi<float>());
      const float r = 0.5;
      const float width = glm::sqrt(r * r - y * y);

      for(int j = 1; j <= numCols; ++j)
      {
        const float x = sin(j * colInc) * width;
        const float z = cos(j * colInc) * width;
        const glm::vec3 normal = glm::normalize(glm::vec3(x,y,z));

        verts.push_back(x);
        verts.push_back(y);
        verts.push_back(z);

        //U,V
        verts.push_back(0.0);
        verts.push_back(0.0);

        verts.push_back(normal.x);
        verts.push_back(normal.y);
        verts.push_back(normal.z);
      }
    }

    //Calculate indices for top pole
    for(int i = 0; i < numCols; ++i)
    {
      const int left = i;
      const int right = (i + 1) % numCols;
      indices.push_back(0);
      indices.push_back(left + firstRingBaseIndex);
      indices.push_back(right + firstRingBaseIndex);
    }

    //Calculate indices for the rows
    for(int i = 0; i < numRows - 2; ++i)
    {
      const int topRowBaseIndex = firstRingBaseIndex + numCols * (i+0);
      const int bottomRowBaseIndex = firstRingBaseIndex + numCols * (i+1);

      for(int j = 0; j < numCols; ++j)
      {
        const int left = j;
        const int right = (j + 1) % numCols;
        indices.push_back(left + topRowBaseIndex);
        indices.push_back(left + bottomRowBaseIndex);
        indices.push_back(right + topRowBaseIndex);

        indices.push_back(right + topRowBaseIndex);
        indices.push_back(left + bottomRowBaseIndex);
        indices.push_back(right + bottomRowBaseIndex);
      }
    }

    //Calculate indices for bottom pole
    for(int i = 0; i < numCols; ++i)
    {
      const int left = i;
      const int right = (i + 1) % numCols;
      indices.push_back(1);
      indices.push_back(left + lastRingBaseIndex);
      indices.push_back(right + lastRingBaseIndex);
    }

    StaticMesh *pMesh = new StaticMesh();
    pMesh->m_iNumTris = indices.size() / 3;
    pMesh->m_iNumIndices = indices.size();
    pMesh->m_iStride = 8 * sizeof(GLfloat);
    pMesh->m_iOffPos = 0 * sizeof(GLfloat);
    pMesh->m_iOffUV = 3 * sizeof(GLfloat);
    pMesh->m_iOffNormal = 5 * sizeof(GLfloat);

    glGenVertexArrays(1, &pMesh->m_vaoConfig);
    glGenBuffers(1, &pMesh->m_vboVertices);
    glGenBuffers(1, &pMesh->m_vboIndices);

    glBindVertexArray(pMesh->m_vaoConfig);

    glBindBuffer(GL_ARRAY_BUFFER, pMesh->m_vboVertices);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(GLfloat), &verts[0], GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pMesh->m_vboIndices);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), &indices[0], GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, pMesh->m_iStride, (void*)pMesh->m_iOffPos);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, pMesh->m_iStride, (void*)pMesh->m_iOffUV);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, pMesh->m_iStride, (void*)pMesh->m_iOffNormal);

    glBindVertexArray(0);

    return pMesh;
  }

}
