#include "SVFIR/SVFType.h"
#include <sstream>
#include "SVFIR/GraphDBClient.h"

namespace SVF
{

SVFType* SVFType::svfI8Ty = nullptr;
SVFType* SVFType::svfPtrTy = nullptr;

__attribute__((weak))
std::string SVFType::toString() const
{
    std::ostringstream os;
    print(os);
    return os.str();
}

std::ostream& operator<<(std::ostream& os, const SVFType& type)
{
    type.print(os);
    return os;
}

void SVFPointerType::print(std::ostream& os) const
{
    os << "ptr";
}

void SVFIntegerType::print(std::ostream& os) const
{
    if (signAndWidth < 0)
        os << 'i' << -signAndWidth;
    else
        os << 'u' << signAndWidth;
}

void SVFFunctionType::print(std::ostream& os) const
{
    os << *getReturnType();
    os << " (";
    for (auto it = params.begin(); it != params.end(); ++it)
    {
        if (it != params.begin())
        {
            os << ", ";
        }
        os << (*it)->toString();
    }
    os << ")";
}

void SVFStructType::print(std::ostream& os) const
{
    os << "S." << name;
}

void SVFArrayType::print(std::ostream& os) const
{
    os << '[' << numOfElement << 'x' << *typeOfElement << ']';
}

void SVFOtherType::print(std::ostream& os) const
{
    os << repr;
}

std::string SVFFunctionType::toDBString() const
{
    std::string is_single_val_ty = isSingleValueType() ? "true" : "false";
    const std::string queryStatement ="CREATE (n:SVFFunctionType {type_name:'" + toString() +
    "', svf_i8_type_name:'" + getSVFInt8Type()->toString() +
    "', svf_ptr_type_name:'" + getSVFPtrType()->toString() + 
    "', kind:" + std::to_string(getKind()) + 
    ", is_single_val_ty:" + is_single_val_ty + 
    ", byte_size:" + std::to_string(getByteSize()) +
    ", params_types_vec:'" + GraphDBClient::getInstance().extractSVFTypes(getParamTypes()) +
    "', ret_ty_node_name:'" + getReturnType()->toString() + "'})";
    return queryStatement;
}

std::string StInfo::toDBString() const
{
    const std::string queryStatement ="CREATE (n:StInfo {id:" + std::to_string(getStinfoId()) +
    ", fld_idx_vec:'" + GraphDBClient::getInstance().extractIdxs(getFlattenedFieldIdxVec()) +
    "', elem_idx_vec:'" + GraphDBClient::getInstance().extractIdxs(getFlattenedElemIdxVec()) + 
    "', finfo_types:'" + GraphDBClient::getInstance().extractSVFTypes(getFlattenFieldTypes()) + 
    "', flatten_element_types:'" + GraphDBClient::getInstance().extractSVFTypes(getFlattenElementTypes()) + 
    "', fld_idx_2_type_map:'" + GraphDBClient::getInstance().extractFldIdx2TypeMap(getFldIdx2TypeMap()) +
    "', stride:" + std::to_string(getStride()) +
    ", num_of_flatten_elements:" + std::to_string(getNumOfFlattenElements()) +
    ", num_of_flatten_fields:" + std::to_string(getNumOfFlattenFields()) + "})";
    return queryStatement;
}

} // namespace SVF
