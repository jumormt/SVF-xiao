//===- SVFModule.h -- SVFModule* class----------------------------------------//
//
//                     SVF: Static Value-Flow Analysis
//
// Copyright (C) <2013-2017>  <Yulei Sui>
//

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.

// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//===----------------------------------------------------------------------===//

/*
 * SVFModule.h
 *
 *  Created on: Aug 4, 2017
 *      Author: Xiaokang Fan
 */

#ifndef INCLUDE_SVFMODULE_H_
#define INCLUDE_SVFMODULE_H_

#include "SVF-LLVM/SVFLLVMValue.h"
#include "Util/NodeIDAllocator.h"
#include "Util/ThreadAPI.h"

namespace SVF
{

class SVFModule
{
    friend class SVFIRWriter;
    friend class SVFIRReader;

public:
    typedef std::vector<const SVFFunction*> FunctionSetType;
    typedef std::vector<SVFLLVMValue*> GlobalSetType;
    typedef std::vector<SVFLLVMValue*> AliasSetType;
    typedef std::vector<SVFConstant*> ConstantType;
    typedef std::vector<SVFLLVMValue*> OtherValueType;

    /// Iterators type def
    typedef FunctionSetType::iterator iterator;
    typedef FunctionSetType::const_iterator const_iterator;
    typedef GlobalSetType::iterator global_iterator;
    typedef GlobalSetType::const_iterator const_global_iterator;
    typedef AliasSetType::iterator alias_iterator;
    typedef AliasSetType::const_iterator const_alias_iterator;
    typedef ConstantType::iterator cdata_iterator;
    typedef ConstantType::const_iterator const_cdata_iterator;
    typedef OtherValueType::iterator ovalue_iterator;
    typedef OtherValueType::const_iterator const_ovalue_iterator;

private:
    static SVFModule* svfModule;
    FunctionSetType FunctionSet;  ///< The Functions in the module
    GlobalSetType GlobalSet;      ///< The Global Variables in the module
    AliasSetType AliasSet;        ///< The Aliases in the module
    ConstantType ConstantSet;     ///< The ConstantData in the module
    OtherValueType OtherValueSet; ///< All other values in the module

    /// Constructors
    SVFModule() = default;

public:
    static SVFModule* getSVFModule();
    static void releaseSVFModule();

    ~SVFModule();


    const SVFFunction* getSVFFunction(const std::string& name);

    ///@{
    inline void addFunctionSet(SVFFunction* svfFunc)
    {
        FunctionSet.push_back(svfFunc);
    }
    inline void addGlobalSet(SVFLLVMValue* glob)
    {
        GlobalSet.push_back(glob);
    }
    inline void addAliasSet(SVFLLVMValue* alias)
    {
        AliasSet.push_back(alias);
    }
    inline void addConstant(SVFConstant* cd)
    {
        ConstantSet.push_back(cd);
    }
    inline void addOtherValue(SVFLLVMValue* ov)
    {
        OtherValueSet.push_back(ov);
    }

    ///@}

    /// Iterators
    ///@{
    global_iterator global_begin()
    {
        return GlobalSet.begin();
    }
    const_global_iterator global_begin() const
    {
        return GlobalSet.begin();
    }
    global_iterator global_end()
    {
        return GlobalSet.end();
    }
    const_global_iterator global_end() const
    {
        return GlobalSet.end();
    }

    alias_iterator alias_begin()
    {
        return AliasSet.begin();
    }
    const_alias_iterator alias_begin() const
    {
        return AliasSet.begin();
    }
    alias_iterator alias_end()
    {
        return AliasSet.end();
    }
    const_alias_iterator alias_end() const
    {
        return AliasSet.end();
    }

    cdata_iterator constant_begin()
    {
        return ConstantSet.begin();
    }
    const_cdata_iterator constant_begin() const
    {
        return ConstantSet.begin();
    }
    cdata_iterator constant_end()
    {
        return ConstantSet.end();
    }
    const_cdata_iterator constant_end() const
    {
        return ConstantSet.end();
    }
    ///@}

    inline const FunctionSetType& getFunctionSet() const
    {
        return FunctionSet;
    }
    inline const ConstantType& getConstantSet() const
    {
        return ConstantSet;
    }
    inline const GlobalSetType& getGlobalSet() const
    {
        return GlobalSet;
    }
    inline const AliasSetType& getAliasSet() const
    {
        return AliasSet;
    }
    inline const OtherValueType& getOtherValueSet() const
    {
        return OtherValueSet;
    }
};

} // End namespace SVF

#endif /* INCLUDE_SVFMODULE_H_ */
