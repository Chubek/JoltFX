/* NLE workflow UI. File analysis/conversion preparation uses scheduler-owned
 * jobs with copied paths; only the UI thread changes the live editor. */
void nle_poll(jfx_desktop_frontend_t *f) {
    auto *j=f->nle_media_job;
    if (!j || !j->done.load(std::memory_order_acquire) || j->operation!=2) return;
    if (desktop_export_running(f)) return;
    if (j->result==JFX_SUCCESS) {
        jfx_export_destroy(f->export_job); f->export_job=j->export_job; j->export_job=nullptr;
        f->export_worker_scene=true; f->export_image_sequence=false;
        f->export_result_snapshot=JFX_SUCCESS;
    } else std::snprintf(f->status,sizeof(f->status),"Conversion: %s",jfx_result_to_string(j->result));
    tilly::destroy(j); f->nle_media_job=nullptr;
}
bool nle_busy(jfx_desktop_frontend_t *f) {
    return f->nle_media_job && !f->nle_media_job->done.load(std::memory_order_acquire);
}
void nle_job(jfx_desktop_frontend_t *f,int operation,const char *path) {
    if (nle_busy(f)) return;
    tilly::destroy(f->nle_media_job); f->nle_media_job=nullptr;
    auto *j=tilly::create<NleMediaJob>();
    if (!j) { editor_result(f,JFX_ERROR_OUT_OF_MEMORY); return; }
    j->operation=operation; j->revision=f->scene_revision; j->track=f->selected_track; j->clip=f->selected_clip;
    std::snprintf(j->path,sizeof(j->path),"%s",path?path:"");
    if (operation==1) {
        j->reference_track=(uint32_t)std::max(0,f->nle_reference_track);
        j->reference_clip=(uint32_t)std::max(0,f->nle_reference_clip);
        const char *ref=jfx_timeline_clip_path(jfx_editor_timeline(f->editor),j->reference_track,j->reference_clip);
        std::snprintf(j->reference,sizeof(j->reference),"%s",ref?ref:"");
    } else if (operation==2) {
        std::snprintf(j->output,sizeof(j->output),"%s",f->nle_convert_output);
        std::snprintf(j->codec,sizeof(j->codec),"%s",f->video_codec);
        j->options.size=sizeof(j->options); j->options.audio=f->video_audio;
        j->options.video_bitrate=(uint64_t)std::max(0,f->nle_bitrate_kbps)*1000;
        j->options.audio_bitrate=(uint64_t)std::max(0,f->nle_audio_bitrate_kbps)*1000;
    }
    if (!scheduler_submit(nle_media_task,j,-1)) { tilly::destroy(j); editor_result(f,JFX_ERROR_BUSY); return; }
    f->nle_media_job=j;
}
void nle_tools(jfx_desktop_frontend_t *f) {
    ImGui::BeginDisabled(f->recording!=nullptr);
    auto *t=jfx_editor_timeline(f->editor);
    auto edit=[&](const char *op,uint32_t a,uint32_t b,uint32_t c,double v,const char *text) {
        editor_result(f,jfx_desktop_frontend_edit(f,op,a,b,c,v,text)); t=jfx_editor_timeline(f->editor);
    };
    if (ImGui::CollapsingHeader("Media bin / relink")) {
        ImGui::InputText("Find media",f->nle_filter,sizeof(f->nle_filter));
        ImGui::TextDisabled("Sequence references; select a use to locate it. Probe reports missing/unsupported media.");
        ImGui::BeginChild("Media references",ImVec2(0,140),true);
        for (uint32_t a=0;a<jfx_timeline_track_count(t);++a) for (uint32_t b=0;b<jfx_timeline_clip_count(t,a);++b) {
            const char *path=jfx_timeline_clip_path(t,a,b);
            if (!path || !*path || (*f->nle_filter && !std::strstr(path,f->nle_filter) && !std::strstr(jfx_timeline_clip_name(t,a,b),f->nle_filter))) continue;
            ImGui::PushID((int)(a*128+b));
            char label[1200]; std::snprintf(label,sizeof(label),"%u:%u  %s | %s",a,b,jfx_timeline_clip_name(t,a,b),path);
            if (ImGui::Selectable(label,f->selected_track==a && f->selected_clip==b)) {
                finish_grading(f); t=jfx_editor_timeline(f->editor);
                f->selected_track=a; f->selected_clip=b; f->nle_first=jfx_timeline_clip_start(t,a,b);
                f->time_seconds=(double)f->nle_first/jfx_timeline_fps(t); f->preview_dirty=true;
                std::snprintf(f->media_path,sizeof(f->media_path),"%s",path);
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::BeginDisabled(nle_busy(f));
        if (ImGui::Button("Probe media")) nle_job(f,0,f->media_path);
        ImGui::EndDisabled();
        auto *job=f->nle_media_job;
        if (job && job->done.load(std::memory_order_acquire) && job->operation==0) {
            ImGui::TextWrapped("%s: %s",job->path,jfx_result_to_string(job->result));
            if (job->result==JFX_SUCCESS) {
                const auto &info=job->info;
                ImGui::Text("%s | %ux%u | %u/%u fps | %.3f s | %s / %s",info.format,info.width,info.height,info.fps_num,info.fps_den,info.duration_seconds,info.video_codec,info.audio_codec);
                if (ImGui::Button("Insert probed media at playhead")) {
                    double count=std::ceil(info.duration_seconds*jfx_timeline_fps(t));
                    uint64_t frame=jfx_desktop_frontend_timeline_current_frame(f);
                    if (frame<=UINT32_MAX) edit("clip.add",f->selected_track,info.video?JFX_CLIP_VIDEO:JFX_CLIP_AUDIO,(uint32_t)frame,count,job->path);
                }
            }
        }
        ImGui::InputText("Replacement media",f->nle_relink_path,sizeof(f->nle_relink_path));
        if (ImGui::Button("Relink all matching uses")) {
            finish_grading(f); t=jfx_editor_timeline(f->editor);
            editor_result(f,jfx_nle_relink(f->editor,jfx_timeline_clip_path(t,f->selected_track,f->selected_clip),f->nle_relink_path));
            t=jfx_editor_timeline(f->editor);
        }
    }
    if (ImGui::CollapsingHeader("Kernels / transitions / captions")) {
        ImGui::InputText("Find kernel",f->nle_kernel_filter,sizeof(f->nle_kernel_filter));
        if (ImGui::BeginCombo("Apply kernel to selected clip","Choose kernel")) {
            for (size_t i=0;i<jfx_node_kind_count();++i) {
                const auto *kind=jfx_node_kind_at(i);
                if (kind->input_count!=1 || kind->inputs[0].type!=JFX_PORT_IMAGE ||
                    (*f->nle_kernel_filter && !std::strstr(kind->name,f->nle_kernel_filter) && !std::strstr(kind->category,f->nle_kernel_filter))) continue;
                char label[192]; std::snprintf(label,sizeof(label),"%s / %s",kind->category,kind->name);
                if (ImGui::Selectable(label)) edit("effect.add",f->selected_track,f->selected_clip,0,0,kind->name);
            }
            ImGui::EndCombo();
        }
        for (uint32_t fx=0;fx<jfx_timeline_effect_count(t,f->selected_track,f->selected_clip);++fx) {
            const auto *kind=jfx_timeline_effect_kind_desc(t,f->selected_track,f->selected_clip,fx);
            ImGui::PushID((int)fx);
            if (ImGui::TreeNode(kind->name)) {
                for (size_t p=0;p<kind->param_count;++p) {
                    float value=jfx_timeline_effect_param_at(t,f->selected_track,f->selected_clip,fx,p,0);
                    if (ImGui::InputFloat(kind->params[p].label,&value,0,0,"%.4f",ImGuiInputTextFlags_EnterReturnsTrue))
                        edit("effect.param",f->selected_track,f->selected_clip,fx,(double)value,kind->params[p].name);
                }
                if (ImGui::SmallButton("Remove kernel")) edit("effect.remove",f->selected_track,f->selected_clip,fx,0,"");
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::InputInt("Transition frames",&f->nle_transition_frames);
        ImGui::TextDisabled("Incoming clip must overlap an earlier clip on this track. Full opacity / normal blend.");
        if (ImGui::Button("Cross dissolve")) edit("clip.transition",f->selected_track,f->selected_clip,0,f->nle_transition_frames,"cross_dissolve");
        ImGui::SameLine(); if (ImGui::Button("Morph cut")) edit("clip.transition",f->selected_track,f->selected_clip,0,f->nle_transition_frames,"morph_cut");
        ImGui::SameLine(); if (ImGui::Button("Remove transition")) edit("clip.transition",f->selected_track,f->selected_clip,0,0,"");
        ImGui::TextDisabled("Morph cut: local small-motion compensation, CPU preview/export.");
        ImGui::InputText("ASS/SSA caption file",f->nle_subtitles,sizeof(f->nle_subtitles));
        ImGui::BeginDisabled(!jfx_nle_subtitles_available());
        if (ImGui::Button("Apply captions")) edit("clip.subtitles",f->selected_track,f->selected_clip,0,0,f->nle_subtitles);
        ImGui::EndDisabled(); ImGui::SameLine();
        if (ImGui::Button("Remove captions")) edit("clip.subtitles",f->selected_track,f->selected_clip,0,0,"");
    }
    if (ImGui::CollapsingHeader("Clap sync / loudness")) {
        ImGui::InputInt("Reference track (zero-based)",&f->nle_reference_track);
        ImGui::InputInt("Reference clip (zero-based)",&f->nle_reference_clip);
        ImGui::TextDisabled("Analyze opening 10 seconds. Selected clip is the microphone/target.");
        ImGui::BeginDisabled(nle_busy(f));
        if (ImGui::Button("Analyze audio / detect clap")) nle_job(f,1,jfx_timeline_clip_path(t,f->selected_track,f->selected_clip));
        ImGui::EndDisabled();
        auto *j=f->nle_media_job;
        if (j && j->operation==1 && j->done.load(std::memory_order_acquire)) {
            ImGui::TextUnformatted(jfx_result_to_string(j->result));
            if (j->result==JFX_SUCCESS) {
                ImGui::Text("Opening loudness %.2f LUFS | sample peak %.3f",j->analysis.integrated_lufs,j->analysis.sample_peak);
                ImGui::Text("Clap target %.4f s (%s), reference %.4f s (%s)",j->analysis.clap_seconds,j->analysis.clap_detected?"detected":"ambiguous/absent",
                    j->reference_analysis.clap_seconds,j->reference_analysis.clap_detected?"detected":"ambiguous/absent");
                bool stale=j->revision!=f->scene_revision;
                if (stale) ImGui::TextDisabled("Sequence changed: analyze again before applying.");
                ImGui::BeginDisabled(stale || !j->analysis.clap_detected || !j->reference_analysis.clap_detected);
                if (ImGui::Button("Align detected claps")) {
                    finish_grading(f);
                    editor_result(f,jfx_nle_sync_clap(f->editor,j->reference_track,j->reference_clip,j->track,j->clip,
                        j->reference_analysis.clap_seconds,j->analysis.clap_seconds));
                    t=jfx_editor_timeline(f->editor);
                }
                ImGui::EndDisabled();
                ImGui::InputFloat("Target LUFS",&f->nle_loudness_target);
                ImGui::BeginDisabled(j->revision!=f->scene_revision || !std::isfinite(j->analysis.integrated_lufs));
                if (ImGui::Button("Set gain from opening loudness")) {
                    double gain=std::pow(10,((double)f->nle_loudness_target-j->analysis.integrated_lufs)/20);
                    if (j->analysis.sample_peak>0) gain=std::min(gain,.98/j->analysis.sample_peak);
                    edit("clip.audio.gain",j->track,j->clip,0,std::min(16.0,gain),"");
                }
                ImGui::EndDisabled();
            }
        }
    }
    if (ImGui::CollapsingHeader("Joltscript edits")) {
        ImGui::TextDisabled("Return [[operation a b c value text] ...]. Entire batch is one undo step.");
        ImGui::InputTextMultiline("##NLE script",f->nle_script,sizeof(f->nle_script),ImVec2(-1,140));
        if (ImGui::Button("Run Joltscript edit")) {
            finish_grading(f); editor_result(f,jfx_nle_run_script(f->editor,f->nle_script,f->nle_script_error,sizeof(f->nle_script_error)));
            t=jfx_editor_timeline(f->editor);
        }
        if (*f->nle_script_error) ImGui::TextWrapped("%s",f->nle_script_error);
    }
    if (ImGui::CollapsingHeader("OpenTimelineIO interchange")) {
        ImGui::TextWrapped("Editorial OTIO JSON: media clips and gaps, names, source trims, rational timing. Effects, overlaps and mixing must be rendered first. Import replaces the sequence and clears history.");
        if (ImGui::Button("Export OTIO to text")) {
            size_t written=0;
            auto r=jfx_nle_export_otio(f->editor,f->nle_otio,sizeof(f->nle_otio),&written);
            if (r!=JFX_SUCCESS) std::snprintf(f->status,sizeof(f->status),"OTIO: %s",jfx_result_to_string(r));
        }
        ImGui::SameLine(); if (ImGui::Button("Import OTIO text")) {
            finish_grading(f); t=jfx_editor_timeline(f->editor);
            editor_result(f,jfx_nle_import_otio(f->editor,f->nle_otio,std::strlen(f->nle_otio),jfx_timeline_width(t),jfx_timeline_height(t),jfx_timeline_fps_num(t),jfx_timeline_fps_den(t)));
        }
        ImGui::InputTextMultiline("##OTIO JSON",f->nle_otio,sizeof(f->nle_otio),ImVec2(-1,120));
    }
    if (ImGui::CollapsingHeader("History")) {
        ImGui::Text("Undo: %zu | Redo: %zu",jfx_editor_history_count(f->editor,false),jfx_editor_history_count(f->editor,true));
        for (size_t i=0;i<jfx_editor_history_count(f->editor,false);++i) ImGui::Text("%zu. %s",i+1,jfx_editor_history_label(f->editor,false,i));
    }
    if (ImGui::CollapsingHeader("Format converter / FFmpeg")) {
        ImGui::Text("Linked FFmpeg: %s",jfx_nle_ffmpeg_version());
        ImGui::InputText("Conversion source",f->nle_convert_source,sizeof(f->nle_convert_source));
        ImGui::InputText("Conversion output",f->nle_convert_output,sizeof(f->nle_convert_output));
        ImGui::TextDisabled("MP4 / MOV / MKV / WebM / WAV. Uses Encoded video export settings; independent of sequence.");
        ImGui::BeginDisabled(nle_busy(f) || desktop_export_running(f));
        if (ImGui::Button("Convert format")) nle_job(f,2,f->nle_convert_source);
        ImGui::EndDisabled();
        if (desktop_export_running(f)) {
            ImGui::ProgressBar((float)((double)desktop_export_completed(f)/(double)desktop_export_total(f)));
            if (ImGui::Button("Cancel conversion")) desktop_export_cancel(f);
        }
    }
    if (nle_busy(f)) ImGui::TextDisabled("Media job running...");
    ImGui::EndDisabled();
}
