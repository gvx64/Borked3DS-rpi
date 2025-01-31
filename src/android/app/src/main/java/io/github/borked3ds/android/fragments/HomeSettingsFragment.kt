// Copyright 2023 Citra Emulator Project
// Copyright 2024 Borked3DS Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package io.github.borked3ds.android.fragments

import android.content.Intent
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.view.ViewGroup.MarginLayoutParams
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import androidx.core.widget.doOnTextChanged
import androidx.documentfile.provider.DocumentFile
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.navigation.findNavController
import androidx.navigation.fragment.findNavController
import androidx.preference.PreferenceManager
import androidx.recyclerview.widget.GridLayoutManager
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.transition.MaterialSharedAxis
import io.github.borked3ds.android.Borked3DSApplication
import io.github.borked3ds.android.HomeNavigationDirections
import io.github.borked3ds.android.R
import io.github.borked3ds.android.adapters.HomeSettingAdapter
import io.github.borked3ds.android.databinding.DialogSoftwareKeyboardBinding
import io.github.borked3ds.android.databinding.FragmentHomeSettingsBinding
import io.github.borked3ds.android.features.settings.model.Settings
import io.github.borked3ds.android.features.settings.ui.SettingsActivity
import io.github.borked3ds.android.features.settings.utils.SettingsFile
import io.github.borked3ds.android.model.Game
import io.github.borked3ds.android.model.HomeSetting
import io.github.borked3ds.android.ui.main.MainActivity
import io.github.borked3ds.android.utils.GameHelper
import io.github.borked3ds.android.utils.GpuDriverHelper
import io.github.borked3ds.android.utils.HomeSettingStringUtils
import io.github.borked3ds.android.utils.Log
import io.github.borked3ds.android.utils.PermissionsHandler
import io.github.borked3ds.android.utils.SearchLocationHelper
import io.github.borked3ds.android.viewmodel.DriverViewModel
import io.github.borked3ds.android.viewmodel.HomeViewModel
import kotlinx.coroutines.flow.MutableStateFlow

class HomeSettingsFragment : Fragment() {
    private var _binding: FragmentHomeSettingsBinding? = null
    private val binding get() = _binding!!

    private lateinit var mainActivity: MainActivity

    private val homeViewModel: HomeViewModel by activityViewModels()
    private val driverViewModel: DriverViewModel by activityViewModels()

    private val preferences
        get() =
            PreferenceManager.getDefaultSharedPreferences(Borked3DSApplication.appContext)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        reenterTransition = MaterialSharedAxis(MaterialSharedAxis.X, false)
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentHomeSettingsBinding.inflate(layoutInflater)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        mainActivity = requireActivity() as MainActivity
        val locations = SearchLocationHelper.getSearchLocations(requireContext())

        val optionsList = listOf(
            HomeSetting(
                HomeSettingStringUtils.ResId(R.string.grid_menu_core_settings),
                HomeSettingStringUtils.ResId(R.string.settings_description),
                R.drawable.ic_settings,
                { SettingsActivity.launch(requireContext(), SettingsFile.FILE_NAME_CONFIG, "") }
            ),
            HomeSetting(
                HomeSettingStringUtils.ResId(R.string.artic_base_connect),
                HomeSettingStringUtils.ResId(R.string.artic_base_connect_description),
                R.drawable.ic_artic_base,
                {
                    val inflater = LayoutInflater.from(context)
                    val inputBinding = DialogSoftwareKeyboardBinding.inflate(inflater)
                    var textInputValue: String =
                        preferences.getString("last_artic_base_addr", "") ?: ""

                    inputBinding.editTextInput.setText(textInputValue)
                    inputBinding.editTextInput.doOnTextChanged { text, _, _, _ ->
                        textInputValue = text?.toString() ?: ""
                    }

                    context?.let {
                        MaterialAlertDialogBuilder(it)
                            .setView(inputBinding.root)
                            .setTitle(getString(R.string.artic_base_enter_address))
                            .setPositiveButton(android.R.string.ok) { _, _ ->
                                if (textInputValue.isNotEmpty()) {
                                    preferences.edit()
                                        .putString("last_artic_base_addr", textInputValue)
                                        .apply()
                                    val menu = Game(
                                        title = getString(R.string.artic_base),
                                        path = "articbase://$textInputValue",
                                        filename = ""
                                    )
                                    val action =
                                        HomeNavigationDirections.actionGlobalEmulationActivity(menu)
                                    binding.root.findNavController().navigate(action)
                                }
                            }
                            .setNegativeButton(android.R.string.cancel) { _, _ -> }
                            .show()
                    }
                }
            ),
            HomeSetting(
                HomeSettingStringUtils.ResId(R.string.system_files),
                HomeSettingStringUtils.ResId(R.string.system_files_description),
                R.drawable.ic_system_update,
                {
                    exitTransition = MaterialSharedAxis(MaterialSharedAxis.X, true)
                    parentFragmentManager.primaryNavigationFragment?.findNavController()
                        ?.navigate(R.id.action_homeSettingsFragment_to_systemFilesFragment)
                }
            ),
            HomeSetting(
                HomeSettingStringUtils.ResId(R.string.install_game_content),
                HomeSettingStringUtils.ResId(R.string.install_game_content_description),
                R.drawable.ic_install,
                { mainActivity.ciaFileInstaller.launch(true) }
            ),
            HomeSetting(
                HomeSettingStringUtils.ResId(R.string.multiplayer),
                HomeSettingStringUtils.ResId(R.string.multiplayer_description),
                R.drawable.ic_network,
                { mainActivity.displayMultiplayerDialog() }
            ),
            HomeSetting(
                HomeSettingStringUtils.ResId(R.string.share_log),
                HomeSettingStringUtils.ResId(R.string.share_log_description),
                R.drawable.ic_share,
                { shareLog() }
            ),
            HomeSetting(
                HomeSettingStringUtils.ResId(R.string.gpu_driver_manager),
                HomeSettingStringUtils.ResId(R.string.install_gpu_driver_description),
                R.drawable.ic_build,
                {
                    binding.root.findNavController()
                        .navigate(R.id.action_homeSettingsFragment_to_driverManagerFragment)
                },
                { GpuDriverHelper.supportsCustomDriverLoading() },
                R.string.custom_driver_not_supported,
                R.string.custom_driver_not_supported_description,
                driverViewModel.selectedDriverMetadata
            ),
            HomeSetting(
                HomeSettingStringUtils.ResId(R.string.select_borked3ds_user_folder),
                HomeSettingStringUtils.ResId(R.string.select_borked3ds_user_folder_home_description),
                R.drawable.ic_home,
                { mainActivity.openBorked3DSDirectory?.launch(null) },
                details = homeViewModel.userDir
            ),
            HomeSetting(
                HomeSettingStringUtils.ResId(R.string.search_location),
                HomeSettingStringUtils.ResId(R.string.search_location_description),
                R.drawable.ic_folder,
                {
                    exitTransition = MaterialSharedAxis(MaterialSharedAxis.X, true)
                    parentFragmentManager.primaryNavigationFragment?.findNavController()
                        ?.navigate(R.id.action_homeSettingsFragment_to_searchLocationFragment)
                },
                { true },
                0,
                0,
                MutableStateFlow<String>(
                    String.format(
                        requireContext().getString(R.string.search_locations_count),
                        if (locations.isEmpty()) "No" else locations.size.toString(),
                        if (locations.size > 1) "s" else ""
                    )
                )
            ),
            HomeSetting(
                HomeSettingStringUtils.ResId(R.string.preferences_theme),
                HomeSettingStringUtils.ResId(R.string.theme_and_color_description),
                R.drawable.ic_palette,
                { SettingsActivity.launch(requireContext(), Settings.SECTION_THEME, "") }
            ),
            HomeSetting(
                HomeSettingStringUtils.ResId(R.string.about),
                HomeSettingStringUtils.ResId(R.string.about_description),
                R.drawable.ic_info_outline,
                {
                    exitTransition = MaterialSharedAxis(MaterialSharedAxis.X, true)
                    parentFragmentManager.primaryNavigationFragment?.findNavController()
                        ?.navigate(R.id.action_homeSettingsFragment_to_aboutFragment)
                }
            )
        )

        binding.homeSettingsList.apply {
            layoutManager = GridLayoutManager(
                requireContext(),
                resources.getInteger(R.integer.game_grid_columns)
            )
            adapter = HomeSettingAdapter(
                requireActivity() as AppCompatActivity,
                viewLifecycleOwner,
                optionsList
            )
        }

        setInsets()
    }

    override fun onStart() {
        super.onStart()
        exitTransition = null
        homeViewModel.setNavigationVisibility(visible = true, animated = true)
        homeViewModel.setStatusBarShadeVisibility(visible = true)
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }

    private val getGamesDirectory =
        registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { result ->
            if (result == null) {
                return@registerForActivityResult
            }

            requireContext().contentResolver.takePersistableUriPermission(
                result,
                Intent.FLAG_GRANT_READ_URI_PERMISSION
            )

            // When a new directory is picked, we currently will reset the existing games
            // database. This effectively means that only one game directory is supported.
            preferences.edit()
                .putString(GameHelper.KEY_GAME_PATH, result.toString())
                .apply()

            Toast.makeText(
                Borked3DSApplication.appContext,
                R.string.games_dir_selected,
                Toast.LENGTH_LONG
            ).show()

            homeViewModel.setGamesDir(requireActivity(), result.path ?: "")
        }

    private fun shareLog() {
        val logDirectory = DocumentFile.fromTreeUri(
            requireContext(),
            PermissionsHandler.borked3dsDirectory ?: return
        )?.findFile("log")
        val currentLog = logDirectory?.findFile("borked3ds_log.txt")
        val oldLog = logDirectory?.findFile("borked3ds_log.txt.old.txt")

        val intent = Intent().apply {
            action = Intent.ACTION_SEND
            type = "text/plain"
        }
        if (!Log.gameLaunched && oldLog?.exists() == true) {
            intent.putExtra(Intent.EXTRA_STREAM, oldLog.uri)
            startActivity(Intent.createChooser(intent, getText(R.string.share_log)))
        } else if (currentLog?.exists() == true) {
            intent.putExtra(Intent.EXTRA_STREAM, currentLog.uri)
            startActivity(Intent.createChooser(intent, getText(R.string.share_log)))
        } else {
            Toast.makeText(
                requireContext(),
                getText(R.string.share_log_not_found),
                Toast.LENGTH_SHORT
            ).show()
        }
    }

    private fun setInsets() =
        ViewCompat.setOnApplyWindowInsetsListener(
            binding.root
        ) { view: View, windowInsets: WindowInsetsCompat ->
            val barInsets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
            val cutoutInsets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())
            val spacingNavigation = resources.getDimensionPixelSize(R.dimen.spacing_navigation)
            val spacingNavigationRail =
                resources.getDimensionPixelSize(R.dimen.spacing_navigation_rail)

            val leftInsets = barInsets.left + cutoutInsets.left
            val rightInsets = barInsets.right + cutoutInsets.right

            binding.scrollViewSettings.updatePadding(
                top = barInsets.top,
                bottom = barInsets.bottom
            )

            val mlpScrollSettings = binding.scrollViewSettings.layoutParams as MarginLayoutParams
            mlpScrollSettings.leftMargin = leftInsets
            mlpScrollSettings.rightMargin = rightInsets
            binding.scrollViewSettings.layoutParams = mlpScrollSettings

            binding.linearLayoutSettings.updatePadding(bottom = spacingNavigation)

            if (view.layoutDirection == View.LAYOUT_DIRECTION_LTR) {
                binding.linearLayoutSettings.updatePadding(left = spacingNavigationRail)
            } else {
                binding.linearLayoutSettings.updatePadding(right = spacingNavigationRail)
            }

            windowInsets
        }
}