/*
---------------------------------------------------------------------------
Open Asset Import Library (ASSIMP)
---------------------------------------------------------------------------

Copyright (c) 2006-2008, ASSIMP Development Team

All rights reserved.

Redistribution and use of this software in source and binary forms, 
with or without modification, are permitted provided that the following 
conditions are met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the above
  copyright notice, this list of conditions and the
  following disclaimer in the documentation and/or other
  materials provided with the distribution.

* Neither the name of the ASSIMP team, nor the names of its
  contributors may be used to endorse or promote products
  derived from this software without specific prior
  written permission of the ASSIMP Development Team.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
---------------------------------------------------------------------------
*/

/** @file Defines a post processing step to search an importer's output
    for data that is obviously invalid  */

#include "AssimpPCH.h"

#ifndef AI_BUILD_NO_FINDINVALIDDATA_PROCESS

// internal headers
#include "FindInvalidDataProcess.h"
#include "ProcessHelper.h"

using namespace Assimp;

// ------------------------------------------------------------------------------------------------
// Constructor to be privately used by Importer
FindInvalidDataProcess::FindInvalidDataProcess()
{
	// nothing to do here
}

// ------------------------------------------------------------------------------------------------
// Destructor, private as well
FindInvalidDataProcess::~FindInvalidDataProcess()
{
	// nothing to do here
}

// ------------------------------------------------------------------------------------------------
// Returns whether the processing step is present in the given flag field.
bool FindInvalidDataProcess::IsActive( unsigned int pFlags) const
{
	return 0 != (pFlags & aiProcess_FindInvalidData);
}

// ------------------------------------------------------------------------------------------------
// Update mesh references in the node graph
void UpdateMeshReferences(aiNode* node, const std::vector<unsigned int>& meshMapping)
{
	if (node->mNumMeshes)
	{
		unsigned int out = 0;
		for (unsigned int a = 0; a < node->mNumMeshes;++a)
		{
			register unsigned int ref = node->mMeshes[a];
			if (0xffffffff != (ref = meshMapping[ref]))
			{
				node->mMeshes[out++] = ref;
			}
		}
		// just let the members that are unused, that's much cheaper
		// than a full array realloc'n'copy party ...
		if(!(node->mNumMeshes = out))
		{
			delete[] node->mMeshes;
			node->mMeshes = NULL;
		}
	}
	// recursively update all children
	for (unsigned int i = 0; i < node->mNumChildren;++i)
		UpdateMeshReferences(node->mChildren[i],meshMapping);
}

// ------------------------------------------------------------------------------------------------
// Executes the post processing step on the given imported data.
void FindInvalidDataProcess::Execute( aiScene* pScene)
{
	DefaultLogger::get()->debug("FindInvalidDataProcess begin");

	bool out = false;
	std::vector<unsigned int> meshMapping(pScene->mNumMeshes);
	unsigned int real = 0, realAnimations = 0;	

	// Process meshes
	for( unsigned int a = 0; a < pScene->mNumMeshes; a++)
	{
		int result;
		if ((result = ProcessMesh( pScene->mMeshes[a])))
		{
			out = true;
			if (2 == result)
			{
				// remove this mesh
				delete pScene->mMeshes[a];
				AI_DEBUG_INVALIDATE_PTR(pScene->mMeshes[a]);

				meshMapping[a] = 0xffffffff;
				continue;
			}
		}
		pScene->mMeshes[real] = pScene->mMeshes[a];
		meshMapping[a] = real++;
	}

	// Process animations
	for (unsigned int a = 0; a < pScene->mNumAnimations;++a)
	{
		int result;
		if ((result = ProcessAnimation( pScene->mAnimations[a])))
		{
			out = true;

			if (2 == result)
			{
				// remove this animation
				delete pScene->mAnimations[a];
				AI_DEBUG_INVALIDATE_PTR(pScene->mAnimations[a]);
				continue;
			}
		}
		pScene->mAnimations[realAnimations++] = pScene->mAnimations[a];
	}

	if (out)
	{
		if(!(pScene->mNumAnimations = realAnimations))
		{
			delete[] pScene->mAnimations;
			pScene->mAnimations = NULL;
		}
		if ( real != pScene->mNumMeshes)
		{
			if (!real)
				throw new ImportErrorException("No meshes remaining");

			
			// we need to remove some meshes.
			// therefore we'll also need to remove all references
			// to them from the scenegraph 
			UpdateMeshReferences(pScene->mRootNode,meshMapping);
			pScene->mNumMeshes = real;
		}

		DefaultLogger::get()->info("FindInvalidDataProcess finished. Found issues ...");
	}
	else DefaultLogger::get()->debug("FindInvalidDataProcess finished. Everything seems to be OK.");
}

// ------------------------------------------------------------------------------------------------
template <typename T>
inline const char* ValidateArrayContents(const T* arr, unsigned int size,
	const std::vector<bool>& dirtyMask)
{
	return NULL;
}

// ------------------------------------------------------------------------------------------------
template <>
inline const char* ValidateArrayContents<aiVector3D>(const aiVector3D* arr, unsigned int size,
	const std::vector<bool>& dirtyMask)
{
	bool b = false;
	for (unsigned int i = 0; i < size;++i)
	{
		if (dirtyMask.size() && dirtyMask[i])continue;

		const aiVector3D& v = arr[i];
		if (is_special_float(v.x) || is_special_float(v.y) || is_special_float(v.z))
		{
			return "INF/NAN was found in a vector component";
		}
		if (i && v != arr[i-1])b = true;
	}
	if (!b)
		return "All vectors are identical";
	return NULL;
}

// ------------------------------------------------------------------------------------------------
template <typename T>
inline bool ProcessArray(T*& in, unsigned int num,const char* name,
	const std::vector<bool>& dirtyMask)
{
	const char* err = ValidateArrayContents(in,num,dirtyMask);
	if (err)
	{
		DefaultLogger::get()->error(std::string("FindInvalidDataProcess fails on mesh ") + name + ": " + err);
		
		delete[] in;
		in = NULL;
		return true;
	}
	return false;
}

// ------------------------------------------------------------------------------------------------
template <typename T>
inline bool AllIdentical(T* in, unsigned int num)
{
	if (!num)return true;
	for (unsigned int i = 0; i < num-1;++i)
	{
		if (in[i] != in[i+1])return false;
	}
	return true;
}

// ------------------------------------------------------------------------------------------------
// Search an animation for invalid content
int FindInvalidDataProcess::ProcessAnimation (aiAnimation* anim)
{
	bool out = false;
	unsigned int real = 0;

	// Process all animation channels
	for (unsigned int a = 0; a < anim->mNumChannels;++a)
	{
		int result;
		if ((result = ProcessAnimationChannel( anim->mChannels[a])))
		{
			out = true;
			// remove this animation channel
			delete anim->mChannels[a];
			AI_DEBUG_INVALIDATE_PTR(anim->mChannels[a]);
			continue;
		}
		anim->mChannels[real++] = anim->mChannels[a];
	}
	if (out)
	{
		anim->mNumChannels = real;
		if (!real)
		{
			DefaultLogger::get()->error("Deleting anim: it consists of dummy tracks");
			return 2;
		}
		return 1;
	}
	return 0;
}

// ------------------------------------------------------------------------------------------------
int FindInvalidDataProcess::ProcessAnimationChannel (aiNodeAnim* anim)
{
	// TODO: (thom) For some reason, even proper channels are deleted as well. Therefore deactivated it for the moment.
	//return 0;

	int i = 0;

	// Check whether all values are identical or whether there is just one keyframe
	if ((anim->mNumPositionKeys < 1 || AllIdentical(anim->mPositionKeys,anim->mNumPositionKeys)) &&
		(anim->mNumScalingKeys  < 1  || AllIdentical(anim->mRotationKeys,anim->mNumRotationKeys)) &&
		(anim->mNumRotationKeys < 1 || AllIdentical(anim->mScalingKeys,anim->mNumScalingKeys)))
	{
		DefaultLogger::get()->error("Deleting dummy position animation channel");
		return 1;
	}
	return 0;
}

// ------------------------------------------------------------------------------------------------
// Search a mesh for invalid contents
int FindInvalidDataProcess::ProcessMesh (aiMesh* pMesh)
{
	bool ret = false;
	std::vector<bool> dirtyMask;

	// process vertex positions
	if(pMesh->mVertices && ProcessArray(pMesh->mVertices,pMesh->mNumVertices,"positions",dirtyMask))
	{
		DefaultLogger::get()->error("Deleting mesh: Unable to continue without vertex positions");
		return 2;
	}

	// process texture coordinates
	for (unsigned int i = 0; i < AI_MAX_NUMBER_OF_TEXTURECOORDS;++i)
	{
		if (!pMesh->mTextureCoords[i])break;
		if (ProcessArray(pMesh->mTextureCoords[i],pMesh->mNumVertices,"uvcoords",dirtyMask))
		{
			// delete all subsequent texture coordinate sets.
			for (unsigned int a = i+1; a < AI_MAX_NUMBER_OF_TEXTURECOORDS;++a)
			{
				delete[] pMesh->mTextureCoords[a]; pMesh->mTextureCoords[a] = NULL;
			}
			ret = true;
		}
	}

	// -- we don't validate vertex colors, it's difficult to say whether
	// they are invalid or not.

	// normals and tangents are undefined for point and line faces.
	// we generate a small lookup table in which we mark all
	// indices into the normals/tangents array that MAY be invalid
	if (pMesh->mNormals || pMesh->mTangents)
	{
		if (aiPrimitiveType_POINT & pMesh->mPrimitiveTypes ||
			aiPrimitiveType_LINE  & pMesh->mPrimitiveTypes)
		{
			if (aiPrimitiveType_TRIANGLE & pMesh->mPrimitiveTypes ||
				aiPrimitiveType_POLYGON  & pMesh->mPrimitiveTypes)
			{
				// we need the lookup table
				dirtyMask.resize(pMesh->mNumVertices,false);
				for (unsigned int m = 0; m < pMesh->mNumFaces;++m)
				{
					const aiFace& f = pMesh->mFaces[m];
					if (2 == f.mNumIndices)
					{
						dirtyMask[f.mIndices[0]] = dirtyMask[f.mIndices[1]] = true;
					}
					else if (1 == f.mNumIndices)dirtyMask[f.mIndices[0]] = true;
				}
			}
			else return ret;
		}

		// process mesh normals
		if (pMesh->mNormals && ProcessArray(pMesh->mNormals,pMesh->mNumVertices,
			"normals",dirtyMask))
			ret = true;

		// process mesh tangents
		if (pMesh->mTangents && ProcessArray(pMesh->mTangents,pMesh->mNumVertices,
			"tangents",dirtyMask))
		{
			delete[] pMesh->mBitangents; pMesh->mBitangents = NULL;
			ret = true;
		}

		// process mesh bitangents
		if (pMesh->mBitangents && ProcessArray(pMesh->mBitangents,pMesh->mNumVertices,
			"bitangents",dirtyMask))
		{
			delete[] pMesh->mTangents; pMesh->mTangents = NULL;
			ret = true;
		}
	}
	return ret ? 1 : 0;
}


#endif // !! AI_BUILD_NO_FINDINVALIDDATA_PROCESS
