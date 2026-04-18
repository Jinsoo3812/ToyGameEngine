#include <sstream>
#include <ostream>
#include <DirectXMath.h>

// Helper functions for streaming matrices
inline std::ostream& operator<<(std::ostream& os, const DirectX::XMFLOAT3& vec)
{
    os << "(" << vec.x << ", " << vec.y << ", " << vec.z << ")";
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const DirectX::XMFLOAT4X4& mat)
{
    os << "["
        << mat._11 << " " << mat._12 << " " << mat._13 << " " << mat._14 << ";\n"
        << mat._21 << " " << mat._22 << " " << mat._23 << " " << mat._24 << ";\n"
        << mat._31 << " " << mat._32 << " " << mat._33 << " " << mat._34 << ";\n"
        << mat._41 << " " << mat._42 << " " << mat._43 << " " << mat._44 << "]";
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const DirectX::XMMATRIX& mat)
{
    os << "["
        << DirectX::XMVectorGetX(mat.r[0]) << " " << DirectX::XMVectorGetY(mat.r[0]) << " " << DirectX::XMVectorGetZ(mat.r[0]) << " " << DirectX::XMVectorGetW(mat.r[0]) << ";\n"
        << DirectX::XMVectorGetX(mat.r[1]) << " " << DirectX::XMVectorGetY(mat.r[1]) << " " << DirectX::XMVectorGetZ(mat.r[1]) << " " << DirectX::XMVectorGetW(mat.r[1]) << ";\n"
        << DirectX::XMVectorGetX(mat.r[2]) << " " << DirectX::XMVectorGetY(mat.r[2]) << " " << DirectX::XMVectorGetZ(mat.r[2]) << " " << DirectX::XMVectorGetW(mat.r[2]) << ";\n"
        << DirectX::XMVectorGetX(mat.r[3]) << " " << DirectX::XMVectorGetY(mat.r[3]) << " " << DirectX::XMVectorGetZ(mat.r[3]) << " " << DirectX::XMVectorGetW(mat.r[3]) << "]";
    return os;
}