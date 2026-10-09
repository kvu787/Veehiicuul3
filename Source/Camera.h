#pragma once
#include "Geometry.h"
#include "SimplePaint/OrthographicTransforms.h"
#include <algorithm>
#include <DirectXMath.h>

struct Camera
{
    Vector3 target{};
    float yaw = .7f, pitch = .55f, height = 7;
    Vector3 TowardCamera() const { return {std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)}; }
    Vector3 Right() const { return {std::cos(yaw),0,-std::sin(yaw)}; }
    Vector3 Up() const { return Cross(TowardCamera(),Right()); }
    Orthographic::ObjectTransforms Transforms(unsigned width, unsigned viewportHeight,DirectX::FXMMATRIX world=DirectX::XMMatrixIdentity()) const
    {
        const auto eye = target + TowardCamera()*(height*4+10);
        const auto view = DirectX::XMMatrixLookAtRH(DirectX::XMVectorSet(eye.x,eye.y,eye.z,1),
            DirectX::XMVectorSet(target.x,target.y,target.z,1),DirectX::XMVectorSet(Up().x,Up().y,Up().z,0));
        const auto projection = Orthographic::MakeProjection(height*float(width)/float(viewportHeight),height,.01f,height*8+100);
        return Orthographic::BuildObjectTransforms(world*view,projection);
    }
    Vector3 Screen(Vector3 world, unsigned width, unsigned viewportHeight) const
    {
        const auto transforms = Transforms(width,viewportHeight);
        const auto& c = transforms.worldToClip;
        auto project = [&](const DirectX::XMFLOAT4& v) { return world.x*v.x+world.y*v.y+world.z*v.z+v.w; };
        return {(project(c[0])+1)*float(width)*.5f,(1-project(c[1]))*float(viewportHeight)*.5f,project(c[2])};
    }
    Vector3 PlanePoint(float x, float y, unsigned width, unsigned viewportHeight) const
    {
        return target+Right()*((x-float(width)*.5f)*height/float(viewportHeight))+Up()*((float(viewportHeight)*.5f-y)*height/float(viewportHeight));
    }
    void Orbit(float x, float y) { yaw -= x*.008f; pitch = std::clamp(pitch+y*.008f,-1.56f,1.56f); }
    void Pan(float x, float y, unsigned viewportHeight) { target += Right()*(-x*height/float(viewportHeight))+Up()*(y*height/float(viewportHeight)); }
    void Zoom(float delta) { height = std::clamp(height*std::exp(-delta*.001f),.05f,10000.0f); }
};
