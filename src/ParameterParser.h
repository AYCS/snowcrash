//
//  ParameterParser.h
//  snowcrash
//
//  Created by Zdenek Nemec on 9/1/13.
//  Copyright (c) 2013 Apiary Inc. All rights reserved.
//

#ifndef SNOWCRASH_PARAMETERPARSER_H
#define SNOWCRASH_PARAMETERPARSER_H

#include "SectionParser.h"
#include "ValuesParser.h"
#include "RegexMatch.h"
#include "StringUtility.h"

/** Parameter Value regex */
#define PARAMETER_VALUE "`([^`]+)`"

/** Parameter Identifier */
#define PARAMETER_IDENTIFIER "(([[:alnum:]_.-])*|(%[A-Fa-f0-9]{2})*)+"

/** Lead in and out for comma separated values regex */
#define CSV_LEADINOUT "[[:blank:]]*,?[[:blank:]]*"

namespace snowcrash {

    /** Parameter Required matching regex */
    const char* const ParameterRequiredRegex = "^[[:blank:]]*[Rr]equired[[:blank:]]*$";

    /** Parameter Optional matching regex */
    const char* const ParameterOptionalRegex = "^[[:blank:]]*[Oo]ptional[[:blank:]]*$";

    /** Additonal Parameter Traits Example matching regex */
    const char* const AdditionalTraitsExampleRegex = CSV_LEADINOUT "`([^`]*)`" CSV_LEADINOUT;

    /** Additonal Parameter Traits Use matching regex */
    const char* const AdditionalTraitsUseRegex = CSV_LEADINOUT "([Oo]ptional|[Rr]equired)" CSV_LEADINOUT;

    /** Additonal Parameter Traits Type matching regex */
    const char* const AdditionalTraitsTypeRegex = CSV_LEADINOUT "([^,]*)" CSV_LEADINOUT;
    
    /** Parameter Values matching regex */
    const char* const ParameterValuesRegex = "^[[:blank:]]*[Vv]alues[[:blank:]]*$";

    /** Values expected content */
    const char* const ExpectedValuesContent = "nested list of possible parameter values, one element per list item e.g. '`value`'";
    
    /** Parameter description delimiter */
    const std::string DescriptionIdentifier = "...";

    /**
     * Parameter section processor
     */
    template<>
    struct SectionProcessor<Parameter> : public SectionProcessorBase<Parameter> {

        static MarkdownNodeIterator processSignature(const MarkdownNodeIterator& node,
                                                     const MarkdownNodes& siblings,
                                                     SectionParserData& pd,
                                                     SectionLayout& layout,
                                                     Report& report,
                                                     Parameter& out) {

            mdp::ByteBuffer signature, remainingContent;
            signature = GetFirstLine(node->text, remainingContent);

            parseSignature(node, pd, signature, report, out);

            if (!remainingContent.empty()) {
                out.description += "\n" + remainingContent + "\n";
            }

            return ++MarkdownNodeIterator(node);
        }


        static MarkdownNodeIterator processNestedSection(const MarkdownNodeIterator& node,
                                                         const MarkdownNodes& siblings,
                                                         SectionParserData& pd,
                                                         Report& report,
                                                         Parameter& out) {

            if (pd.sectionContext() != ValuesSectionType) {
                return node;
            }

            // Check redefinition
            if (!out.values.empty()) {
                // WARN: parameter values are already defined
                std::stringstream ss;
                ss << "overshadowing previous 'values' definition";
                ss << " for parameter '" << out.name << "'";

                mdp::CharactersRangeSet sourceMap = mdp::BytesRangeSetToCharactersRangeSet(node->sourceMap, pd.sourceData);
                report.warnings.push_back(Warning(ss.str(),
                                                  RedefinitionWarning,
                                                  sourceMap));
            }

            // Clear any previous values
            out.values.clear();

            ValuesParser::parse(node, siblings, pd, report, out.values);

            if (out.values.empty()) {
                // WARN: empty definition
                std::stringstream ss;
                ss << "no possible values specified for parameter '" << out.name << "'";

                mdp::CharactersRangeSet sourceMap = mdp::BytesRangeSetToCharactersRangeSet(node->sourceMap, pd.sourceData);
                report.warnings.push_back(Warning(ss.str(),
                                                  EmptyDefinitionWarning,
                                                  sourceMap));
            }

            if ((!out.exampleValue.empty() || !out.defaultValue.empty()) &&
                 !out.values.empty()) {

                checkExampleAndDefaultValue(node, pd, report, out);
            }

            return ++MarkdownNodeIterator(node);
        }

        static SectionType sectionType(const MarkdownNodeIterator& node) {

            if (node->type == mdp::ListItemMarkdownNodeType
                && !node->children().empty()) {

                mdp::ByteBuffer subject, remainingContent;
                subject = GetFirstLine(node->children().front().text, remainingContent);
                TrimString(subject);

                if (isValidParameterSignature(subject)) {
                    return ParameterSectionType;
                }
            }

            return UndefinedSectionType;
        }

        static SectionType nestedSectionType(const MarkdownNodeIterator& node) {

            return SectionProcessor<Values>::sectionType(node);
        }

        static SectionTypes nestedSectionTypes() {
            SectionTypes nested;

            nested.push_back(ValuesSectionType);

            return nested;
        }

        static void parseSignature(const mdp::MarkdownNodeIterator& node,
                                   SectionParserData& pd,
                                   mdp::ByteBuffer& signature,
                                   Report& report,
                                   Parameter& parameter) {

            parameter.use = UndefinedParameterUse;

            TrimString(signature);

            CaptureGroups captureGroups;

            if (isValidParameterSignature(signature)) {
                
                mdp::ByteBuffer innerSignature = signature;
                innerSignature = TrimString(innerSignature);
                
                size_t firstSpace = innerSignature.find(" ");

                if (firstSpace == std::string::npos) {
                    // Name
                    parameter.name = signature;
                }
                else {
                    parameter.name = innerSignature.substr(0, firstSpace);
                    innerSignature = innerSignature.substr(firstSpace + 1);
                    size_t descriptionPos = innerSignature.find(snowcrash::DescriptionIdentifier);

                    if (descriptionPos != std::string::npos) {
                        // Description
                        parameter.description = innerSignature.substr(descriptionPos);
                        parameter.description = TrimString(parameter.description.replace(0, snowcrash::DescriptionIdentifier.length(), ""));
                        innerSignature = innerSignature.substr(0, descriptionPos);
                        innerSignature = TrimString(innerSignature);
                    }
                    
                    size_t attributesPos = innerSignature.find("(");

                    if (attributesPos != std::string::npos) {
                        size_t endOfAttributesPos = innerSignature.find_last_of(")");

                        if (endOfAttributesPos - attributesPos > 1) {
                            std::string attributes = innerSignature.substr(attributesPos, endOfAttributesPos - attributesPos);
                            attributes = attributes.substr(1);
                            parseAdditionalTraits(node, pd, attributes, report, parameter);
                            innerSignature = innerSignature.substr(0, attributesPos);
                            innerSignature = TrimString(innerSignature);
                        }
                    }
                    
                    if (innerSignature.length() > 0) {
                        // Remove =
                        parameter.defaultValue = innerSignature;
                        parameter.defaultValue.erase(std::remove(parameter.defaultValue.begin(), parameter.defaultValue.end(), '='), parameter.defaultValue.end());
                        parameter.defaultValue.erase(std::remove(parameter.defaultValue.begin(), parameter.defaultValue.end(), '`'), parameter.defaultValue.end());
                        parameter.defaultValue = TrimString(parameter.defaultValue);
                    }
                }

                // Check possible required vs default clash
                if (parameter.use != OptionalParameterUse &&
                    !parameter.defaultValue.empty()) {
                    
                    // WARN: Required vs default clash
                    std::stringstream ss;
                    ss << "specifying parameter '" << parameter.name << "' as required supersedes its default value"\
                          ", declare the parameter as 'optional' to specify its default value";

                    mdp::CharactersRangeSet sourceMap = mdp::BytesRangeSetToCharactersRangeSet(node->sourceMap, pd.sourceData);
                    report.warnings.push_back(Warning(ss.str(),
                                                      LogicalErrorWarning,
                                                      sourceMap));
                }
            } else {
                // ERR: unable to parse
                mdp::CharactersRangeSet sourceMap = mdp::BytesRangeSetToCharactersRangeSet(node->sourceMap, pd.sourceData);
                report.error = Error("unable to parse parameter specification",
                                     BusinessError,
                                     sourceMap);
            }
        }

        static void parseAdditionalTraits(const mdp::MarkdownNodeIterator& node,
                                          SectionParserData& pd,
                                          mdp::ByteBuffer& traits,
                                          Report& report,
                                          Parameter& parameter) {

            TrimString(traits);

            CaptureGroups captureGroups;

            // Cherry pick example value, if any
            if (RegexCapture(traits, AdditionalTraitsExampleRegex, captureGroups) &&
                captureGroups.size() > 1) {

                parameter.exampleValue = captureGroups[1];
                std::string::size_type pos = traits.find(captureGroups[0]);

                if (pos != std::string::npos) {
                    traits.replace(pos, captureGroups[0].length(), std::string());
                }
            }

            captureGroups.clear();

            // Cherry pick use attribute, if any
            if (RegexCapture(traits, AdditionalTraitsUseRegex, captureGroups) &&
                captureGroups.size() > 1) {

                parameter.use = RegexMatch(captureGroups[1], ParameterOptionalRegex) ? OptionalParameterUse : RequiredParameterUse;
                std::string::size_type pos = traits.find(captureGroups[0]);

                if (pos != std::string::npos) {
                    traits.replace(pos, captureGroups[0].length(), std::string());
                }
            }

            captureGroups.clear();

            // Finish with type
            if (RegexCapture(traits, AdditionalTraitsTypeRegex, captureGroups) &&
                captureGroups.size() > 1) {

                parameter.type = captureGroups[1];
                std::string::size_type pos = traits.find(captureGroups[0]);

                if (pos != std::string::npos) {
                    traits.replace(pos, captureGroups[0].length(), std::string());
                }
            }

            // Check what is left
            TrimString(traits);

            if (!traits.empty()) {
                // WARN: Additional parameters traits warning
                std::stringstream ss;
                ss << "unable to parse additional parameter traits";
                ss << ", expected '([required | optional], [<type>], [`<example value>`])'";
                ss << ", e.g. '(optional, string, `Hello World`)'";

                mdp::CharactersRangeSet sourceMap = mdp::BytesRangeSetToCharactersRangeSet(node->sourceMap, pd.sourceData);
                report.warnings.push_back(Warning(ss.str(),
                                                  FormattingWarning,
                                                  sourceMap));

                parameter.type.clear();
                parameter.exampleValue.clear();
                parameter.use = UndefinedParameterUse;
            }
        }

        static void checkExampleAndDefaultValue(const mdp::MarkdownNodeIterator& node,
                                                SectionParserData& pd,
                                                Report& report,
                                                Parameter& parameter) {

            bool isExampleFound = false;
            bool isDefaultFound = false;

            std::stringstream ss;
            bool printWarning = false;

            for (Collection<Value>::iterator it = parameter.values.begin();
                 it != parameter.values.end();
                 ++it) {

                if (parameter.exampleValue == *it) {
                    isExampleFound = true;
                }

                if (parameter.defaultValue == *it) {
                    isDefaultFound = true;
                }
            }

            if(!parameter.exampleValue.empty() &&
               !isExampleFound) {

                // WARN: missing example in values.
                ss << "the example value '" << parameter.exampleValue << "' of parameter '"<< parameter.name <<"' is not in its list of expected values";
                printWarning = true;
            }

            if(!parameter.defaultValue.empty() &&
               !isDefaultFound) {

                // WARN: missing default in values.
                ss << "the default value '" << parameter.defaultValue << "' of parameter '"<< parameter.name <<"' is not in its list of expected values";
                printWarning = true;
            }

            if (printWarning) {
                mdp::CharactersRangeSet sourceMap = mdp::BytesRangeSetToCharactersRangeSet(node->sourceMap, pd.sourceData);
                report.warnings.push_back(Warning(ss.str(),
                                                  LogicalErrorWarning,
                                                  sourceMap));
            }
        }
        
        /** Determine if a signature is a valid parameter*/
        static bool isValidParameterSignature(const mdp::ByteBuffer& signature) {
            
            mdp::ByteBuffer innerSignature = signature;
            innerSignature = TrimString(innerSignature);
            
            if (innerSignature.length() == 0) {
                return false; // Empty string, invalid
            }
            
            size_t firstSpace = innerSignature.find(" ");

            if (firstSpace == std::string::npos) {
                return RegexMatch(innerSignature, "^" PARAMETER_IDENTIFIER "$");
            }
            
            std::string paramName = innerSignature.substr(0, firstSpace);

            if (!RegexMatch(paramName, "^" PARAMETER_IDENTIFIER "$")) {
                return false; // Invalid param name
            }

            // Remove param name
            innerSignature = innerSignature.substr(firstSpace + 1);
            size_t descriptionPos = innerSignature.find(snowcrash::DescriptionIdentifier);

            // Remove description
            if (descriptionPos != std::string::npos) {
                innerSignature = innerSignature.substr(0, descriptionPos);
                innerSignature = TrimString(innerSignature);
            }
            
            size_t attributesPos = innerSignature.find("(");

            if (attributesPos != std::string::npos) {
                size_t endOfAttributesPos = innerSignature.find_last_of(")");

                if (endOfAttributesPos == std::string::npos) {
                    return false; // Expecting close of attributes
                }

                // Remove attributes
                innerSignature = innerSignature.substr(0, attributesPos);
                innerSignature = TrimString(innerSignature);
            }
            
            if (innerSignature.length() == 0) {
                return true;
            }
            
            if (innerSignature.substr(0,1) == "=") {
                innerSignature = innerSignature.substr(1);
                innerSignature = TrimString(innerSignature);

                if (innerSignature.length() == 0) {
                    return false; // No default value
                }

                if (innerSignature.substr(0,1) == "`" && innerSignature.substr(innerSignature.length()-1,1) == "`") {
                    return true;
                }
            }
            
            return false;
        }

    };

    /** Parameter Section Parser */
    typedef SectionParser<Parameter, ListSectionAdapter> ParameterParser;
}

#endif