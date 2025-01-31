/**
 *    Copyright (c) 2023 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#import "MTRConversion.h"
#import "MTRLogging_Internal.h"

#include <lib/support/SafeInt.h>
#include <lib/support/TimeUtils.h>

CHIP_ERROR SetToCATValues(NSSet<NSNumber *> * catSet, chip::CATValues & values)
{
    values = chip::kUndefinedCATs;

    unsigned long long tagCount = catSet.count;
    if (tagCount > chip::kMaxSubjectCATAttributeCount) {
        MTR_LOG_ERROR("%llu CASE Authenticated Tags cannot be represented in a certificate.", tagCount);
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    size_t tagIndex = 0;
    for (NSNumber * boxedTag in [catSet.allObjects sortedArrayUsingSelector:@selector(compare:)]) {
        auto unboxedTag = boxedTag.unsignedLongLongValue;
        if (!chip::CanCastTo<chip::CASEAuthTag>(unboxedTag)) {
            MTR_LOG_ERROR("0x%llx is not a valid CASE Authenticated Tag value.", unboxedTag);
            return CHIP_ERROR_INVALID_ARGUMENT;
        }

        auto tag = static_cast<chip::CASEAuthTag>(unboxedTag);
        if (!chip::IsValidCASEAuthTag(tag)) {
            MTR_LOG_ERROR("0x%" PRIx32 " is not a valid CASE Authenticated Tag value.", tag);
            return CHIP_ERROR_INVALID_ARGUMENT;
        }

        values.values[tagIndex++] = tag;
    }

    return CHIP_NO_ERROR;
}

NSSet<NSNumber *> * CATValuesToSet(const chip::CATValues & values)
{
    auto * catSet = [[NSMutableSet alloc] initWithCapacity:values.GetNumTagsPresent()];
    for (auto & value : values.values) {
        if (value != chip::kUndefinedCAT) {
            [catSet addObject:@(value)];
        }
    }
    return [NSSet setWithSet:catSet];
}

bool DateToMatterEpochSeconds(NSDate * date, uint32_t & matterEpochSeconds)
{
    NSCalendar * calendar = [[NSCalendar alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian];
    NSDateComponents * components = [calendar componentsInTimeZone:[NSTimeZone timeZoneForSecondsFromGMT:0] fromDate:date];

    if (!chip::CanCastTo<uint16_t>(components.year)) {
        return false;
    }

    uint16_t year = static_cast<uint16_t>([components year]);
    uint8_t month = static_cast<uint8_t>([components month]);
    uint8_t day = static_cast<uint8_t>([components day]);
    uint8_t hour = static_cast<uint8_t>([components hour]);
    uint8_t minute = static_cast<uint8_t>([components minute]);
    uint8_t second = static_cast<uint8_t>([components second]);
    return chip::CalendarToChipEpochTime(year, month, day, hour, minute, second, matterEpochSeconds);
}
