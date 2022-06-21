/*
* Licensed to the Apache Software Foundation (ASF) under one
* or more contributor license agreements.  See the NOTICE file
* distributed with this work for additional information
* regarding copyright ownership.  The ASF licenses this file
* to you under the Apache License, Version 2.0 (the
* License); you may not use this file except in compliance
* with the License.  You may obtain a copy of the License at
*
*   http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing,
* software distributed under the License is distributed on an
* AS IS BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
* KIND, either express or implied.  See the License for the
* specific language governing permissions and limitations
* under the License.
*/

/*
* Copyright (c) 2022, AXERA TECH
* Author: hebing
*/
#include <cstring>
#include <numeric>
#include <stdio.h>

#include <opencv2/opencv.hpp>
#include <fcntl.h>
#include <sys/mman.h>

#include "base/topk.hpp"
#include "base/yolo.hpp"

#include "middleware/io.hpp"

#include "utilities/args.hpp"
#include "utilities/cmdline.hpp"
#include "utilities/file.hpp"
#include "utilities/timer.hpp"

#include "ax_interpreter_external_api.h"
#include "ax_sys_api.h"
#include "joint.h"
#include "joint_adv.h"
#include "base/detection.hpp"
#include "base/common.hpp"

const int DEFAULT_LOOP_COUNT = 1;

const float PROB_THRESHOLD = 0.20f;
const float NMS_THRESHOLD = 0.45f;

const char* CLASS_NAMES[] = {
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat", "traffic light",
    "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep", "cow",
    "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
    "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove", "skateboard", "surfboard",
    "tennis racket", "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple",
    "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch",
    "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse", "remote", "keyboard", "cell phone",
    "microwave", "oven", "toaster", "sink", "refrigerator", "book", "clock", "vase", "scissors", "teddy bear",
    "hair drier", "toothbrush"};

const float ANCHORS[18] = {10, 13, 16, 30, 33, 23, 30, 61, 62, 45, 59, 119, 116, 90, 156, 198, 373, 326};

namespace ax
{
    namespace cls = classification;
    namespace mw = middleware;
    namespace utl = utilities;
    namespace det = detection;

    bool run_yolov5(const std::string& model, const std::string& image_dir, const std::string& val_file, const std::string& output_file, int input_size)
    {
        // 1. create a runtime handle and load the model
        AX_JOINT_HANDLE joint_handle;
        std::memset(&joint_handle, 0, sizeof(joint_handle));

        AX_JOINT_SDK_ATTR_T joint_attr;
        std::memset(&joint_attr, 0, sizeof(joint_attr));

        // 1.1 read model file to buffer
        auto* file_fp = fopen(model.c_str(), "r");
        if (!file_fp)
        {
            fprintf(stderr, "read model file fail \n");
            return false;
        }

        fseek(file_fp, 0, SEEK_END);
        int model_size = ftell(file_fp);
        fclose(file_fp);

        int fd = open(model.c_str(), O_RDWR, 0644);
        void* mmap_add = mmap(NULL, model_size, PROT_WRITE, MAP_SHARED, fd, 0);

        //        auto ret = ax::mw::parse_npu_mode_from_joint((const AX_CHAR*)mmap_add, model_size, &joint_attr.eNpuMode);
        //        if (AX_ERR_NPU_JOINT_SUCCESS != ret)
        //        {
        //            fprintf(stderr, "Load Run-Joint model(%s) failed.\n", model.c_str());
        //            return false;
        //        }

        joint_attr.eNpuMode = AX_NPU_SDK_EX_HARD_MODE_T::AX_NPU_VIRTUAL_1_1;

        // 1.3 init model
        auto ret = AX_JOINT_Adv_Init(&joint_attr);
        if (AX_ERR_NPU_JOINT_SUCCESS != ret)
        {
            fprintf(stderr, "Init Run-Joint model(%s) failed.\n", model.c_str());
            return false;
        }

        auto deinit_joint = [&joint_handle, &mmap_add, model_size]() {
            AX_JOINT_DestroyHandle(joint_handle);
            AX_JOINT_Adv_Deinit();
            munmap(mmap_add, model_size);
            return false;
        };

        // 1.4 the real init processing
        uint32_t duration_hdl_init_us = 0;
        {
            timer init_timer;
            ret = AX_JOINT_CreateHandle(&joint_handle, mmap_add, model_size);
            duration_hdl_init_us = (uint32_t)(init_timer.cost() * 1000);
            if (AX_ERR_NPU_JOINT_SUCCESS != ret)
            {
                fprintf(stderr, "Create Run-Joint handler from file(%s) failed.\n", model.c_str());
                return deinit_joint();
            }
        }

        // 1.5 get the version of toolkit (optional)
        const AX_CHAR* version = AX_JOINT_GetModelToolsVersion(joint_handle);
        fprintf(stdout, "Tools version: %s\n", version);

        // std::vector<char>().swap(model_buffer);
        auto io_info = AX_JOINT_GetIOInfo(joint_handle);

        // 1.7 create context
        AX_JOINT_EXECUTION_CONTEXT joint_ctx;
        AX_JOINT_EXECUTION_CONTEXT_SETTING_T joint_ctx_settings;
        std::memset(&joint_ctx, 0, sizeof(joint_ctx));
        std::memset(&joint_ctx_settings, 0, sizeof(joint_ctx_settings));
        ret = AX_JOINT_CreateExecutionContextV2(joint_handle, &joint_ctx, &joint_ctx_settings);
        if (AX_ERR_NPU_JOINT_SUCCESS != ret)
        {
            fprintf(stderr, "Create Run-Joint context failed.\n");
            return deinit_joint();
        }

        // 2. fill input & prepare to inference
        AX_JOINT_IO_T joint_io_arr;
        AX_JOINT_IO_SETTING_T joint_io_setting;
        std::memset(&joint_io_arr, 0, sizeof(joint_io_arr));
        std::memset(&joint_io_setting, 0, sizeof(joint_io_setting));

        auto clear_and_exit = [&joint_io_arr, &joint_ctx, &joint_handle, &mmap_add, model_size]() {
            for (size_t i = 0; i < joint_io_arr.nInputSize; ++i)
            {
                AX_JOINT_IO_BUFFER_T* pBuf = joint_io_arr.pInputs + i;
                mw::free_joint_buffer(pBuf);
            }
            for (size_t i = 0; i < joint_io_arr.nOutputSize; ++i)
            {
                AX_JOINT_IO_BUFFER_T* pBuf = joint_io_arr.pOutputs + i;
                mw::free_joint_buffer(pBuf);
            }
            delete[] joint_io_arr.pInputs;
            delete[] joint_io_arr.pOutputs;

            AX_JOINT_DestroyExecutionContext(joint_ctx);
            AX_JOINT_DestroyHandle(joint_handle);
            AX_JOINT_Adv_Deinit();
            munmap(mmap_add, model_size);

            return false;
        };

        // 3. get the init profile info.
        AX_JOINT_COMPONENT_T* joint_comps;
        uint32_t joint_comp_size;

        ret = AX_JOINT_ADV_GetComponents(joint_ctx, &joint_comps, &joint_comp_size);
        if (AX_ERR_NPU_JOINT_SUCCESS != ret)
        {
            fprintf(stderr, "Get components failed.\n");
            return clear_and_exit();
        }

        uint32_t duration_neu_init_us = 0;
        uint32_t duration_axe_init_us = 0;
        for (uint32_t j = 0; j < joint_comp_size; ++j)
        {
            auto& comp = joint_comps[j];
            switch (comp.eType)
            {
            case AX_JOINT_COMPONENT_TYPE_T::AX_JOINT_COMPONENT_TYPE_NEU:
            {
                duration_neu_init_us += comp.tProfile.nInitUs;
                break;
            }
            case AX_JOINT_COMPONENT_TYPE_T::AX_JOINT_COMPONENT_TYPE_AXE:
            {
                duration_axe_init_us += comp.tProfile.nInitUs;
                break;
            }
            default:
                fprintf(stderr, "Unknown component type %d.\n", (int)comp.eType);
            }
        }

        // prepare
        int image_size = input_size * input_size * 3;
        auto pBuf = mw::prepare_io_no_copy(image_size, joint_io_arr, io_info);
        if (!pBuf)
        {
            fprintf(stderr, "[ERR] prepare_io_no_copy fail \n");
            clear_and_exit();
        }

        // 4. run & benchmark
        uint32_t duration_neu_core_us = 0, duration_neu_total_us = 0;
        uint32_t duration_axe_core_us = 0, duration_axe_total_us = 0;

        std::ifstream val_file_1000(val_file);
        if (!val_file_1000.is_open())
        {
            fprintf(stderr, "[ERR] val_file_1000 open fail \n");
            clear_and_exit();
        }

        std::vector<float> time_costs;
        std::vector<float> time_postprocess;
        std::string val_file_1000_line_temp;
        std::vector<uint8_t> image(input_size * input_size * 3);
        float prob_threshold_unsigmoid = -1.0f * (float)log((1.0f / PROB_THRESHOLD) - 1.0f);

        FILE* file_handle = fopen(output_file.c_str(), "w");
        fprintf(file_handle, "[");
        bool is_first = true;

        int index = 0;

        while (getline(val_file_1000, val_file_1000_line_temp))
        {
            // 1.0 decode file path
            std::stringstream val_1000_line_ss(val_file_1000_line_temp);
            std::string file_name, file_name_index;
            getline(val_1000_line_ss, file_name, ' ');
            getline(val_1000_line_ss, file_name_index, ' ');
            std::string image_file_path = image_dir + file_name;

            // 1.1 prepare image precess
            cv::Mat mat = cv::imread(image_file_path);
            if (mat.empty())
            {
                fprintf(stderr, "Read image failed.\n");
                clear_and_exit();
            }
            cv::cvtColor(mat, mat, cv::COLOR_BGR2RGB);
            common::get_input_data_letterbox(mat, image, input_size, input_size);
            //            cv::Mat img_new(input_size, input_size, CV_8UC3, image.data());
            //            cv::resize(mat, img_new, cv::Size(input_size, input_size));

            //ret = mw::prepare_io(image.data(), image.size(), joint_io_arr, io_info);
            ret = mw::copy_to_device(image.data(), image.size(), pBuf);
            if (AX_ERR_NPU_JOINT_SUCCESS != ret)
            {
                fprintf(stderr, "Fill copy_to_device failed.\n");
                AX_JOINT_DestroyExecutionContext(joint_ctx);
                return deinit_joint();
            }
            joint_io_arr.pIoSetting = &joint_io_setting;

            timer tick;
            ret = AX_JOINT_RunSync(joint_handle, joint_ctx, &joint_io_arr);

            if (AX_ERR_NPU_JOINT_SUCCESS != ret)
            {
                fprintf(stderr, "Inference failed(%d).\n", ret);
                return clear_and_exit();
            }

            std::vector<det::Object> proposals;
            std::vector<det::Object> objects;

            timer forward_time;
            for (uint32_t i = 0; i < io_info->nOutputSize; ++i)
            {
                auto& output = io_info->pOutputs[i];
                auto& info = joint_io_arr.pOutputs[i];

                auto ptr = (float*)info.pVirAddr;

                int32_t stride = (1 << i) * 8;
                det::generate_proposals_255(stride, ptr, PROB_THRESHOLD, proposals, input_size, input_size, ANCHORS, prob_threshold_unsigmoid);
            }
            det::get_out_bbox(proposals, objects, NMS_THRESHOLD, input_size, input_size, mat.rows, mat.cols);
            time_postprocess.push_back(forward_time.cost());

            for (size_t i = 0; i < objects.size(); i++)
            {
                det::Object object = objects[i];

                if (is_first)
                {
                    fprintf(file_handle, "{\"image_id\":%d, \"category_id\":%d, \"bbox\":[%.3f,%.3f,%.3f,%.3f], \"score\":%.6f}",
                            std::stoi(file_name_index), object.label, object.rect.x, object.rect.y, object.rect.width, object.rect.height, object.prob);
                    is_first = false;
                }
                else
                {
                    fprintf(file_handle, ",{\"image_id\":%d, \"category_id\":%d, \"bbox\":[%.3f,%.3f,%.3f,%.3f], \"score\":%.6f}",
                            std::stoi(file_name_index), object.label, object.rect.x, object.rect.y, object.rect.width, object.rect.height, object.prob);
                }
            }

            time_costs.push_back(tick.cost());

            ret = AX_JOINT_ADV_GetComponents(joint_ctx, &joint_comps, &joint_comp_size);
            if (AX_ERR_NPU_JOINT_SUCCESS != ret)
            {
                fprintf(stderr, "Get components after run failed.\n");
                return clear_and_exit();
            }

            for (uint32_t j = 0; j < joint_comp_size; ++j)
            {
                auto& comp = joint_comps[j];

                if (comp.eType == AX_JOINT_COMPONENT_TYPE_T::AX_JOINT_COMPONENT_TYPE_NEU)
                {
                    duration_neu_core_us += comp.tProfile.nCoreUs;
                    duration_neu_total_us += comp.tProfile.nTotalUs;
                }

                if (comp.eType == AX_JOINT_COMPONENT_TYPE_T::AX_JOINT_COMPONENT_TYPE_AXE)
                {
                    duration_axe_core_us += comp.tProfile.nCoreUs;
                    duration_axe_total_us += comp.tProfile.nTotalUs;
                }
            }

            if (index < 10)
            {
                detection::draw_objects(mat, objects, CLASS_NAMES, file_name.c_str());
            }
            index++;
        }

        fprintf(file_handle, "]");
        fclose(file_handle);

        // 6. show time costs
        fprintf(stdout, "--------------------------------------\n");
        fprintf(stdout,
                "Create handle took %.2f ms (neu %.2f ms, axe %.2f ms, overhead %.2f ms)\n",
                duration_hdl_init_us / 1000.,
                duration_neu_init_us / 1000.,
                duration_axe_init_us / 1000.,
                (duration_hdl_init_us - duration_neu_init_us - duration_axe_init_us) / 1000.);

        fprintf(stdout, "--------------------------------------\n");

        auto total_time = std::accumulate(time_costs.begin(), time_costs.end(), 0.f);
        auto min_max_time = std::minmax_element(time_costs.begin(), time_costs.end());
        fprintf(stdout,
                "run model %d times, avg time %.2f ms, max_time %.2f ms, min_time %.2f ms\n",
                time_costs.size(),
                total_time / (float)time_costs.size(),
                *min_max_time.second,
                *min_max_time.first);

        clear_and_exit();
        return true;
    }
} // namespace ax

int main(int argc, char* argv[])
{
    cmdline::parser cmd;
    cmd.add<std::string>("model", 'm', "joint file(a.k.a. joint model)", true, "");
    cmd.add<std::string>("images", 'i', "image file", true, "");
    cmd.add<std::string>("val", 'v', "val file", true, "");
    cmd.add<std::string>("out", 'o', "output file path", false, "./out.json");

    cmd.parse_check(argc, argv);

    // 0. get app args, can be removed from user's app
    auto model_file = cmd.get<std::string>("model");
    auto image_file = cmd.get<std::string>("images");
    auto val_file = cmd.get<std::string>("val");
    auto output_file = cmd.get<std::string>("out");

    auto model_file_flag = utilities::file_exist(model_file);
    auto val_file_flag = utilities::file_exist(val_file);

    if (!model_file_flag | !val_file_flag)
    {
        auto show_error = [](const std::string& kind, const std::string& value) {
            fprintf(stderr, "Input file %s(%s) is not exist, please check it.\n", kind.c_str(), value.c_str());
        };

        if (!model_file_flag) { show_error("model", model_file); }
        if (!val_file_flag) { show_error("val", image_file); }

        return -1;
    }

    // 1. print args
    fprintf(stdout, "--------------------------------------\n");

    fprintf(stdout, "model file : %s\n", model_file.c_str());
    fprintf(stdout, "val file : %s\n", val_file.c_str());

    // 3. init ax system, if NOT INITED in other apps.
    //   if other app init the device, DO NOT INIT DEVICE AGAIN.
    //   this init ONLY will be used in demo apps to avoid using a none inited
    //     device.
    //   for example, if another app(such as camera init app) has already init
    //     the system, then DO NOT call this api again, if it does, the system
    //     will be re-inited and loss the last configuration.
    AX_S32 ret = AX_SYS_Init();
    if (0 != ret)
    {
        fprintf(stderr, "Init system failed.\n");
        return ret;
    }

    // 4. show the version (optional)
    fprintf(stdout, "Run-Joint Runtime version: %s\n", AX_JOINT_GetVersion());
    fprintf(stdout, "--------------------------------------\n");

    // 5. run the processing

    auto flag = ax::run_yolov5(model_file, image_file, val_file, output_file, 640);
    if (!flag)
    {
        fprintf(stderr, "Run classification failed.\n");
    }

    // 6. last de-init
    //   as step 1, if the device inited by another app, DO NOT de-init the
    //     device at this app.
    AX_SYS_Deinit();
}