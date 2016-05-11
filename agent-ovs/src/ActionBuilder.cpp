/*
 * Copyright (c) 2014-2016 Cisco Systems, Inc. and others.  All rights reserved.
 *
 * This program and the accompanying materials are made available under the
 * terms of the Eclipse Public License v1.0 which accompanies this distribution,
 * and is available at http://www.eclipse.org/legal/epl-v10.html
 */

#include <algorithm>
#include <boost/assert.hpp>

#include "ovs.h"
#include "ActionBuilder.h"

extern const struct mf_field mf_fields[MFF_N_IDS];

using boost::asio::ip::address;
using boost::asio::ip::address_v4;
using boost::asio::ip::address_v6;

namespace ovsagent {

ActionBuilder::ActionBuilder(FlowBuilder& fb_)
    : flowHasVlan(false), fb(fb_) {
    ofpbuf_init(&buf, 64);
}

ActionBuilder::ActionBuilder()
    : flowHasVlan(false) {
    ofpbuf_init(&buf, 64);
}

ActionBuilder::~ActionBuilder() {
    ofpbuf_uninit(&buf);
}

ofpact * ActionBuilder::getActionsFromBuffer(ofpbuf *buf, size_t& actsLen) {
    ofpact_pad(buf);
    actsLen = buf->size;
    return (ofpact*)ofpbuf_steal_data(buf);
}

void ActionBuilder::build(ofputil_flow_stats *dstEntry) {
    dstEntry->ofpacts = getActionsFromBuffer(&buf, dstEntry->ofpacts_len);
}

void ActionBuilder::build(ofputil_flow_mod *dstMod) {
    dstMod->ofpacts = getActionsFromBuffer(&buf, dstMod->ofpacts_len);
}

void ActionBuilder::build(ofputil_packet_out *dstPkt) {
    dstPkt->ofpacts = getActionsFromBuffer(&buf, dstPkt->ofpacts_len);
}

void ActionBuilder::build(ofputil_bucket *dstBucket) {
    dstBucket->ofpacts = getActionsFromBuffer(&buf, dstBucket->ofpacts_len);
}

FlowBuilder& ActionBuilder::parent() {
    return fb.get();
}

static void initSubField(struct mf_subfield *sf, enum mf_field_id id) {
    const struct mf_field *field = &mf_fields[(int)id];
    sf->field = field;
    sf->ofs = 0;                   /* start position */
    sf->n_bits = field->n_bits;    /* number of bits */
}

ActionBuilder& ActionBuilder::reg8(mf_field_id regId, uint8_t regValue) {
    struct ofpact_set_field *load = ofpact_put_reg_load(&buf);
    load->field = &mf_fields[(int)regId];
    load->value.u8 = regValue;
    load->mask.u8 = 0xff;
    return *this;
}

ActionBuilder& ActionBuilder::reg16(mf_field_id regId, uint16_t regValue) {
    struct ofpact_set_field *load = ofpact_put_reg_load(&buf);
    load->field = &mf_fields[(int)regId];
    load->value.be16 = htons(regValue);
    load->mask.be16 = ~((uint16_t)0);
    return *this;
}

ActionBuilder& ActionBuilder::reg(mf_field_id regId, uint32_t regValue) {
    struct ofpact_set_field *load = ofpact_put_reg_load(&buf);
    load->field = &mf_fields[(int)regId];
    load->value.be32 = htonl(regValue);
    load->mask.be32 = ~((uint32_t)0);
    return *this;
}

ActionBuilder& ActionBuilder::reg64(mf_field_id regId, uint64_t regValue) {
    struct ofpact_set_field *load = ofpact_put_reg_load(&buf);
    load->field = &mf_fields[(int)regId];
    load->value.be64 = htonll(regValue);
    load->mask.be64 = ~((uint64_t)0);
    return *this;
}

ActionBuilder& ActionBuilder::reg(mf_field_id regId, const uint8_t *macValue) {
    struct ofpact_set_field *load = ofpact_put_reg_load(&buf);
    load->field = &mf_fields[(int)regId];
    memcpy(&(load->value.mac), macValue, ETH_ADDR_LEN);
    memset(&(load->mask.mac), 0xff, ETH_ADDR_LEN);
    return *this;
}

ActionBuilder& ActionBuilder::regMove(mf_field_id srcRegId,
                                      mf_field_id dstRegId) {
    struct ofpact_reg_move *move = ofpact_put_REG_MOVE(&buf);
    initSubField(&move->src, srcRegId);
    initSubField(&move->dst, dstRegId);

    int bitsToMove = std::min(move->src.n_bits, move->dst.n_bits);
    move->src.n_bits = bitsToMove;
    move->dst.n_bits  = bitsToMove;
    return *this;
}

ActionBuilder& ActionBuilder::metadata(uint64_t metadata, uint64_t mask) {
    struct ofpact_metadata* meta = ofpact_put_WRITE_METADATA(&buf);
    meta->metadata = htonll(metadata);
    meta->mask = htonll(mask);
    return *this;
}

ActionBuilder& ActionBuilder::ethSrc(const uint8_t *srcMac) {
    if (srcMac) {
        struct ofpact_set_field *sf = ofpact_put_SET_FIELD(&buf);
        sf->field = &mf_fields[MFF_ETH_SRC];
        memcpy(&(sf->value.mac), srcMac, ETH_ADDR_LEN);
        memset(&(sf->mask.mac), 0xff, ETH_ADDR_LEN);
        sf->flow_has_vlan = flowHasVlan;
    }
    return *this;
}

ActionBuilder& ActionBuilder::ethDst(const uint8_t *dstMac) {
    if (dstMac) {
        struct ofpact_set_field *sf = ofpact_put_SET_FIELD(&buf);
        sf->field = &mf_fields[MFF_ETH_DST];
        memcpy(&(sf->value.mac), dstMac, ETH_ADDR_LEN);
        memset(&(sf->mask.mac), 0xff, ETH_ADDR_LEN);
        sf->flow_has_vlan = flowHasVlan;
    }
    return *this;
}

ActionBuilder& ActionBuilder::ipSrc(const address& srcIp) {
    if (srcIp.is_v4()) {
        struct ofpact_ipv4 *set = ofpact_put_SET_IPV4_SRC(&buf);
        set->ipv4 = htonl(srcIp.to_v4().to_ulong());
    } else {
        struct ofpact_set_field *sf = ofpact_put_SET_FIELD(&buf);
        sf->field = &mf_fields[MFF_IPV6_SRC];
        memcpy(&(sf->value.ipv6), srcIp.to_v6().to_bytes().data(),
               sizeof(sf->value.ipv6));
        memset(&(sf->mask.ipv6), 0xff, sizeof(sf->mask.ipv6));
    }
    return *this;
}

ActionBuilder& ActionBuilder::ipDst(const address& dstIp) {
    if (dstIp.is_v4()) {
        struct ofpact_ipv4 *set = ofpact_put_SET_IPV4_DST(&buf);
        set->ipv4 = htonl(dstIp.to_v4().to_ulong());
    } else {
        struct ofpact_set_field *sf = ofpact_put_SET_FIELD(&buf);
        sf->field = &mf_fields[MFF_IPV6_DST];
        memcpy(&(sf->value.ipv6), dstIp.to_v6().to_bytes().data(),
               sizeof(sf->value.ipv6));
        memset(&(sf->mask.ipv6), 0xff, sizeof(sf->mask.ipv6));
    }
    return *this;
}

ActionBuilder& ActionBuilder::decTtl() {
    struct ofpact_cnt_ids *ctlr = ofpact_put_DEC_TTL(&buf);
    uint16_t ctlrId = 0;
    ofpbuf_put(&buf, &ctlrId, sizeof(ctlrId));
    ctlr = (ofpact_cnt_ids*)buf.header;      // needed because of put() above
    ctlr->n_controllers = 1;
    ofpact_update_len(&buf, &ctlr->ofpact);
    return *this;
}

ActionBuilder& ActionBuilder::go(uint8_t tableId) {
    struct ofpact_goto_table *goTab = ofpact_put_GOTO_TABLE(&buf);
    goTab->table_id = tableId;
    return *this;
}

ActionBuilder& ActionBuilder::resubmit(uint32_t inPort, uint8_t tableId) {
    struct ofpact_resubmit *resubmit = ofpact_put_RESUBMIT(&buf);
    resubmit->in_port = inPort;
    resubmit->table_id = tableId;
    return *this;
}

ActionBuilder& ActionBuilder::output(uint32_t port) {
    struct ofpact_output *output = ofpact_put_OUTPUT(&buf);
    output->port = port;
    return *this;
}

ActionBuilder& ActionBuilder::outputReg(mf_field_id srcRegId) {
    struct ofpact_output_reg *outputReg = ofpact_put_OUTPUT_REG(&buf);
    outputReg->max_len = UINT16_MAX;
    assert(outputReg->ofpact.raw == (uint8_t)(-1));
    initSubField(&outputReg->src, srcRegId);
    return *this;
}

ActionBuilder& ActionBuilder::group(uint32_t groupId) {
    ofpact_group *group = ofpact_put_GROUP(&buf);
    group->group_id = groupId;
    return *this;
}

ActionBuilder& ActionBuilder::controller(uint16_t max_len) {
    struct ofpact_output *contr = ofpact_put_OUTPUT(&buf);
    contr->port = OFPP_CONTROLLER;
    contr->max_len = max_len;
    return *this;
}

ActionBuilder& ActionBuilder::pushVlan() {
    ofpact_put_PUSH_VLAN(&buf);
    flowHasVlan = true;
    return *this;
}

ActionBuilder& ActionBuilder::setVlanVid(uint16_t vlan) {
    ofpact_vlan_vid* act = ofpact_put_SET_VLAN_VID(&buf);
    act->vlan_vid = (0xfff & vlan);
    act->push_vlan_if_needed = 0;
    act->flow_has_vlan = true;
    return *this;
}

ActionBuilder& ActionBuilder::popVlan() {
    /* ugly hack to avoid the fact that there's no way in the API to
       make a pop vlan action */
    ofpact_put_STRIP_VLAN(&buf)->ofpact.raw = 8;
    return *this;
}

} // namespace ovsagent



